// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/archive/archive_reader.h"

#include "augra/log.h"

#include "dos/archive/archive_identity.h"
#include "dos/archive/descriptor.h"
#include "dos/archive/entry_names.h"
#include "dos/archive/exclusive_file.h"
#include "dos/archive/zip_central_directory.h"
#include "utils/checks.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <functional>
#include <optional>
#include <random>
#include <span>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#if defined(WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

CHECK_NARROWING();

namespace ArchiveMount {

namespace {

constexpr auto LogComponent     = "archive";
constexpr size_t ReadBlockBytes = 64 * 1024;

#if defined(WIN32)
constexpr int OpenFlags = _O_RDONLY | _O_BINARY | _O_NOINHERIT;
#else
constexpr int OpenFlags = O_RDONLY | O_CLOEXEC;
#endif

void close_fd(const int fd)
{
#if defined(WIN32)
	_close(fd);
#else
	close(fd);
#endif
}

struct Descriptor {
	int fd = -1;
	~Descriptor()
	{
		if (fd >= 0) {
			close_fd(fd);
		}
	}
};

int open_read_only(const std::filesystem::path& path)
{
#if defined(WIN32)
	return _wopen(path.wstring().c_str(), OpenFlags);
#else
	return open(path.c_str(), OpenFlags);
#endif
}

int duplicate_at_start(const int fd)
{
	const auto dup_fd = DuplicateNoInherit(fd);
	if (dup_fd >= 0) {
#if defined(WIN32)
		_lseeki64(dup_fd, 0, SEEK_SET);
#else
		lseek(dup_fd, 0, SEEK_SET);
#endif
	}
	return dup_fd;
}

struct StatPair {
	int64_t size  = 0;
	int64_t mtime = 0;
};

bool stat_fd(const int fd, StatPair& out)
{
#if defined(WIN32)
	struct _stat64 st = {};
	if (_fstat64(fd, &st) != 0) {
		return false;
	}
#else
	struct stat st = {};
	if (fstat(fd, &st) != 0) {
		return false;
	}
#endif
	out.size  = static_cast<int64_t>(st.st_size);
	out.mtime = static_cast<int64_t>(st.st_mtime);
	return true;
}

// libarchive's fd client never closes the descriptor it is given, so
// the handle owns its duplicate and closes it after the free.
struct Handle {
	struct archive* a = nullptr;
	int fd            = -1;
	~Handle()
	{
		if (a) {
			archive_read_free(a);
		}
		if (fd >= 0) {
			close_fd(fd);
		}
	}
};

enum class HandleStatus {
	Ok,
	DescriptorFailed,
	NotRecognized, // libarchive bids the format at open; no bidder took it
};

// The signature at offset 0 picks the reader, not libarchive's bid: a RAR
// holding an ISO with zips on it carries a zip end record in its tail that
// outbids the RAR reader. tar has no signature and keeps the full set.
enum class Signature { None, Zip, Rar4, Rar5, SevenZip };

// The first bytes of the file, read once through a duplicate; every
// sniff below is a pure function of them.
struct Head {
	std::array<uint8_t, 64> bytes = {};
	size_t size                   = 0;
	std::span<const uint8_t> view() const
	{
		return {bytes.data(), size};
	}
};

Head read_head(const int fd)
{
	auto head         = Head{};
	const auto dup_fd = duplicate_at_start(fd);
	if (dup_fd < 0) {
		return head;
	}
#if defined(WIN32)
	const auto got = _read(dup_fd,
	                       head.bytes.data(),
	                       static_cast<unsigned int>(head.bytes.size()));
#else
	const auto got = read(dup_fd, head.bytes.data(), head.bytes.size());
#endif
	close_fd(dup_fd);
	head.size = got > 0 ? static_cast<size_t>(got) : 0;
	return head;
}

bool starts_with(const std::span<const uint8_t> data,
                 const std::span<const uint8_t> magic)
{
	return data.size() >= magic.size() &&
	       std::equal(magic.begin(), magic.end(), data.begin());
}

constexpr std::array<uint8_t, 7> Rar4Magic = {'R', 'a', 'r', '!', 0x1a, 0x07, 0x00};
constexpr std::array<uint8_t, 8> Rar5Magic = {'R', 'a', 'r', '!', 0x1a, 0x07, 0x01, 0x00};
constexpr std::array<uint8_t, 6> SevenZipMagic = {'7', 'z', 0xbc, 0xaf, 0x27, 0x1c};
constexpr std::array<uint8_t, 4> ZipLocalMagic = {'P', 'K', 0x03, 0x04};

Signature sniff_signature(const Head& head)
{
	if (starts_with(head.view(), Rar5Magic)) {
		return Signature::Rar5;
	}
	if (starts_with(head.view(), Rar4Magic)) {
		return Signature::Rar4;
	}
	if (starts_with(head.view(), SevenZipMagic)) {
		return Signature::SevenZip;
	}
	if (starts_with(head.view(), ZipLocalMagic)) {
		return Signature::Zip;
	}
	return Signature::None;
}

// Only the D12 filters, and only where the library decodes them itself:
// every filter answers ARCHIVE_WARN when it would run an external program
// instead (filter_all adds lrzip and grzip, which always do).
struct FilterSet {
	std::vector<int (*)(struct archive*)> usable = {};
	ZipCentralDirectory::Decoders decoders       = {};
};

const FilterSet& probe_filters()
{
	static const auto set = [] {
		struct Probe {
			const char* name;
			int (*support)(struct archive*);
			bool ZipCentralDirectory::Decoders::* decoder;
		};
		using Decoders       = ZipCentralDirectory::Decoders;
		const Probe probes[] = {
		        { "gzip",  archive_read_support_filter_gzip,          nullptr},
		        {"bzip2", archive_read_support_filter_bzip2, &Decoders::bzip2},
		        {   "xz",    archive_read_support_filter_xz,    &Decoders::xz},
		        { "zstd",  archive_read_support_filter_zstd,  &Decoders::zstd}
                };
		auto result = FilterSet{};
		for (const auto& probe : probes) {
			struct archive* trial = archive_read_new();
			const auto r          = probe.support(trial);
			archive_read_free(trial);
			if (r == ARCHIVE_OK) {
				result.usable.push_back(probe.support);
			} else {
				if (probe.decoder) {
					result.decoders.*probe.decoder = false;
				}
				augra::log_warn(LogComponent,
				                "libarchive has no built-in %s decoder; "
				                "archives and zip members using it are refused",
				                probe.name);
			}
		}
		return result;
	}();
	return set;
}

void register_filters(struct archive* a)
{
	for (const auto support : probe_filters().usable) {
		support(a);
	}
}

// One read handle over a dup of the kept descriptor: libarchive cannot
// rewind, and opening by descriptor keeps a swapped file out (design 1.6).
// The dups share one file offset, so only one handle reads at a time.
HandleStatus open_handle(const int fd, const Signature signature,
                         Handle& handle, std::string* detail)
{
	handle.a = archive_read_new();
	switch (signature) {
	case Signature::Zip:
		archive_read_support_format_zip_seekable(handle.a);
		break;
	case Signature::Rar4: archive_read_support_format_rar(handle.a); break;
	case Signature::Rar5: archive_read_support_format_rar5(handle.a); break;
	case Signature::SevenZip:
		archive_read_support_format_7zip(handle.a);
		break;
	case Signature::None:
		archive_read_support_format_zip_seekable(handle.a);
		archive_read_support_format_rar(handle.a);
		archive_read_support_format_rar5(handle.a);
		archive_read_support_format_7zip(handle.a);
		archive_read_support_format_tar(handle.a);
		break;
	}
	register_filters(handle.a);
	// Where copyfile.h exists libarchive hides __MACOSX entries by
	// default; every host must walk the same set for the pairing to hold.
	archive_read_set_format_option(handle.a, "zip", "mac-ext", nullptr);
	// tar carries bytes; the house writes UTF-8 names into tars
	archive_read_set_format_option(handle.a, "tar", "hdrcharset", "UTF-8");
	handle.fd = duplicate_at_start(fd);
	if (handle.fd < 0) {
		if (detail) {
			*detail = "cannot duplicate the archive descriptor";
		}
		return HandleStatus::DescriptorFailed;
	}
	if (archive_read_open_fd(handle.a, handle.fd, ReadBlockBytes) != ARCHIVE_OK) {
		if (detail) {
			const auto* text = archive_error_string(handle.a);
			*detail          = text ? text : "open failed";
		}
		return HandleStatus::NotRecognized;
	}
	return HandleStatus::Ok;
}

const char* zip_method_name(const uint16_t method)
{
	switch (method) {
	case 1: return "shrunk";
	case 2:
	case 3:
	case 4:
	case 5: return "reduced";
	case 6: return "imploded";
	case 7: return "tokenized";
	case 9: return "deflate64";
	case 10: return "PKWARE DCL imploded";
	case 99: return "AES encrypted";
	default: return "unknown";
	}
}

bool has_control_bytes(const std::string& raw)
{
	for (const auto c : raw) {
		const auto byte = static_cast<unsigned char>(c);
		if (byte < 0x20 || byte == 0x7f) {
			return true;
		}
	}
	return false;
}

std::optional<Format> format_of(struct archive* a)
{
	switch (archive_format(a) & ARCHIVE_FORMAT_BASE_MASK) {
	case ARCHIVE_FORMAT_ZIP: return Format::Zip;
	case ARCHIVE_FORMAT_RAR: return Format::Rar;
	case ARCHIVE_FORMAT_RAR_V5: return Format::Rar5;
	case ARCHIVE_FORMAT_7ZIP: return Format::SevenZip;
	case ARCHIVE_FORMAT_TAR: return Format::Tar;
	default: return {};
	}
}

Access access_of(const Format format)
{
	switch (format) {
	case Format::Zip:
	case Format::Rar: return Access::PerMember;
	case Format::Rar5:
	case Format::SevenZip:
	case Format::Tar: return Access::Sequential;
	}
	return Access::Sequential;
}

// RAR v4 main header (4.x technote, "Archive header"): 7-byte signature,
// block CRC (2), type (1, 0x73), flags (2); MHD_VOLUME bit 0, MHD_SOLID
// bit 3. libarchive reports solid only at data read, too late for a mount.
constexpr uint16_t Rar4FlagVolume = 0x0001;
constexpr uint16_t Rar4FlagSolid  = 0x0008;

std::optional<uint16_t> rar4_main_flags(const Head& head)
{
	const auto bytes = head.view();
	if (bytes.size() < 12 || !starts_with(bytes, Rar4Magic) || bytes[9] != 0x73) {
		return {};
	}
	return static_cast<uint16_t>(bytes[10] | (bytes[11] << 8));
}

// RAR 5.0 main header (rarlab technote): signature (8), CRC32, vints for
// header size, type (1 = main), header flags, an extra-area size if flag
// bit 0, then the archive flags (bit 0 = volume, which libarchive never says).
constexpr uint64_t Rar5ArchiveFlagVolume = 0x0001;

std::optional<uint64_t> rar5_archive_flags(const Head& head)
{
	const auto bytes = head.view();
	if (bytes.size() < 12 || !starts_with(bytes, Rar5Magic)) {
		return {};
	}
	size_t pos      = 12;
	const auto vint = [&](uint64_t& out) {
		out = 0;
		for (auto shift = 0; shift < 64; shift += 7) {
			if (pos >= bytes.size()) {
				return false;
			}
			const auto byte = bytes[pos++];
			out |= static_cast<uint64_t>(byte & 0x7f) << shift;
			if (!(byte & 0x80)) {
				return true;
			}
		}
		return false;
	};
	// Header flags bit 0 announces an extra area and bit 1 a data area;
	// each adds a size vint before the archive flags.
	uint64_t header_size = 0, header_type = 0, header_flags = 0,
	         skipped = 0, archive_flags = 0;
	if (!vint(header_size) || !vint(header_type) || !vint(header_flags) ||
	    header_type != 1) {
		return {};
	}
	if ((header_flags & 0x01) && !vint(skipped)) {
		return {};
	}
	if ((header_flags & 0x02) && !vint(skipped)) {
		return {};
	}
	if (!vint(archive_flags)) {
		return {};
	}
	return archive_flags;
}

std::string random_suffix()
{
	auto device = std::random_device{};
	auto out    = std::string();
	for (auto i = 0; i < 4; ++i) {
		const auto word = device();
		for (auto shift = 0; shift < 32; shift += 4) {
			out.push_back("0123456789abcdef"[(word >> shift) & 0xf]);
		}
	}
	return out;
}

// The file clock holds about 1882 to 2466 on libstdc++; a FILETIME of
// zero in a 7z (1601) or a base-256 tar mtime would overflow it.
constexpr int64_t MinStampSeconds = 0;
constexpr int64_t MaxStampSeconds = 4102444800; // 2100-01-01

// No clock_cast on AlmaLinux 8's libstdc++. The two clocks' epoch gap is
// whole seconds everywhere, so the nanoseconds between the two reads are
// rounded away; unrounded, clang's read order stamped a second early.
std::filesystem::file_time_type file_time_from_unix(const int64_t seconds)
{
	using file_duration = std::filesystem::file_time_type::duration;
	const auto sys_now  = std::chrono::system_clock::now();
	const auto file_now = std::filesystem::file_time_type::clock::now();
	const auto gap      = std::chrono::round<std::chrono::seconds>(
                file_now.time_since_epoch() -
                std::chrono::duration_cast<file_duration>(sys_now.time_since_epoch()));
	return std::filesystem::file_time_type(std::chrono::duration_cast<file_duration>(
	        gap + std::chrono::seconds(
	                      std::clamp(seconds, MinStampSeconds, MaxStampSeconds))));
}

// DOS date and time packed as the zip stores them (time low, date
// high). For RAR4 the fields come back out of libarchive's local-time
// conversion, which is exact except inside a DST gap.
int64_t identity_time_of(const Format format, const int64_t mtime,
                         const uint32_t zip_dos_time)
{
	switch (format) {
	case Format::Zip: return zip_dos_time;
	case Format::Rar: {
		const auto t = static_cast<time_t>(mtime);
		struct tm tm = {};
#if defined(WIN32)
		localtime_s(&tm, &t);
#else
		localtime_r(&t, &tm);
#endif
		const auto date = ((tm.tm_year - 80) << 9) |
		                  ((tm.tm_mon + 1) << 5) | tm.tm_mday;
		const auto time = (tm.tm_hour << 11) | (tm.tm_min << 5) |
		                  (tm.tm_sec / 2);
		return (static_cast<int64_t>(date) << 16) | time;
	}
	case Format::Rar5:
	case Format::SevenZip:
	case Format::Tar: return mtime;
	}
	return mtime;
}

const char* error_text_or(struct archive* a, const char* fallback)
{
	const auto* text = archive_error_string(a);
	return text ? text : fallback;
}

} // namespace

struct ArchiveReader::Impl {
	Descriptor file                                 = {};
	std::filesystem::path path                      = {};
	Signature signature                             = Signature::None;
	Limits limits                                   = {};
	Format format                                   = Format::Zip;
	Access access                                   = Access::Sequential;
	StatPair indexed_stat                           = {};
	std::vector<Entry> entries                      = {};
	std::unordered_map<std::string, size_t> by_path = {};
	std::unordered_map<int, size_t> by_ordinal      = {};
	int64_t total_bytes                             = 0;
	std::string identity                            = {};
	// Every handle shares one file offset, so two calls at once would
	// interleave their reads; the guard makes a second caller fail loudly.
	std::atomic<bool> in_use = false;

	struct Use {
		std::atomic<bool>& flag;
		explicit Use(std::atomic<bool>& f) : flag(f)
		{
			const auto was = flag.exchange(true);
			assert(!was && "ArchiveReader used from two threads at once");
			(void)was;
		}
		~Use()
		{
			flag = false;
		}
	};

	// The listing's time column per entry: zip and RAR4 store DOS date and
	// time, which libarchive converts through mktime in the host's zone;
	// the identity uses the DOS fields themselves, the same on every host.
	std::vector<int64_t> identity_times = {};

	void add(Entry entry, const int64_t identity_time)
	{
		by_path[entry.utf8_path] = entries.size();
		if (entry.ordinal >= 0) {
			by_ordinal[entry.ordinal] = entries.size();
		}
		identity_times.push_back(identity_time);
		entries.push_back(std::move(entry));
	}

	const Entry* at_path(const std::string& utf8_path) const
	{
		const auto it = by_path.find(utf8_path);
		return it == by_path.end() ? nullptr : &entries[it->second];
	}

	Error check_unchanged(std::string* detail) const
	{
		auto now = StatPair{};
		if (!stat_fd(file.fd, now) || now.size != indexed_stat.size ||
		    now.mtime != indexed_stat.mtime) {
			if (detail) {
				*detail = "archive size or mtime changed since it was indexed";
			}
			return Error::Changed;
		}
		return Error::None;
	}

	// Reads exactly the declared size; more or less is corruption.
	Error read_member(struct archive* a, const Entry& entry,
	                  const std::function<bool(const uint8_t*, size_t)>& sink,
	                  std::string* detail) const
	{
		std::vector<uint8_t> block(ReadBlockBytes);
		int64_t total = 0;
		while (true) {
			const auto got = archive_read_data(a,
			                                   block.data(),
			                                   block.size());
			if (got < 0) {
				if (detail) {
					*detail = error_text_or(a, "read failed");
				}
				return Error::Corrupt;
			}
			if (got == 0) {
				break;
			}
			total += got;
			if (total > entry.size) {
				if (detail) {
					*detail = "member longer than its declared size";
				}
				return Error::Corrupt;
			}
			if (!sink(block.data(), static_cast<size_t>(got))) {
				if (detail) {
					*detail = "cannot write the member";
				}
				return Error::OpenFailed;
			}
		}
		if (total != entry.size) {
			if (detail) {
				*detail = "member shorter than its declared size";
			}
			return Error::Corrupt;
		}
		return Error::None;
	}

	const Entry* find(const int ordinal) const
	{
		const auto it = by_ordinal.find(ordinal);
		return it == by_ordinal.end() ? nullptr : &entries[it->second];
	}

	// Temp name beside the target, renamed only after the declared size
	// arrived and libarchive's CRC check passed, so a crash never leaves a
	// short member under the real name (design 2.1).
	Error write_member(struct archive* a, const Entry& entry,
	                   const std::filesystem::path& target,
	                   std::string* detail) const
	{
		std::error_code ec = {};
		std::filesystem::create_directories(target.parent_path(), ec);
		const auto temp = target.parent_path() /
		                  (target.filename().string() + "." +
		                   random_suffix() + ".part");
		FILE* f = OpenExclusive(temp);
		if (!f) {
			if (detail) {
				*detail = "cannot create " + temp.string();
			}
			return Error::OpenFailed;
		}
		const auto read = read_member(
		        a,
		        entry,
		        [&](const uint8_t* data, const size_t size) {
			        return fwrite(data, 1, size, f) == size;
		        },
		        detail);
		const auto closed = fclose(f) == 0;
		if (read != Error::None || !closed) {
			if (read == Error::None && detail) {
				*detail = "cannot finish writing " + temp.string();
			}
			std::filesystem::remove(temp, ec);
			return read != Error::None ? read : Error::OpenFailed;
		}
		if (entry.mtime < MinStampSeconds || entry.mtime > MaxStampSeconds) {
			augra::log_warn(LogComponent,
			                "%s: mtime %lld of '%s' is outside the file clock, clamped",
			                path.string().c_str(),
			                static_cast<long long>(entry.mtime),
			                target.string().c_str());
		}
		std::filesystem::last_write_time(temp,
		                                 file_time_from_unix(entry.mtime),
		                                 ec);
		if (ec) {
			augra::log_warn(LogComponent,
			                "%s: cannot set the mtime of '%s': %s",
			                path.string().c_str(),
			                target.string().c_str(),
			                ec.message().c_str());
			ec.clear();
		}
		std::filesystem::rename(temp, target, ec);
		if (ec) {
			if (detail) {
				*detail = "cannot rename into " + target.string();
			}
			std::filesystem::remove(temp, ec);
			return Error::OpenFailed;
		}
		return Error::None;
	}

	Error seek_to(Handle& handle, const int ordinal, std::string* detail) const
	{
		if (open_handle(file.fd, signature, handle, detail) !=
		    HandleStatus::Ok) {
			return Error::OpenFailed;
		}
		struct archive_entry* ae = nullptr;
		for (auto index = 0;; ++index) {
			const auto r = archive_read_next_header(handle.a, &ae);
			if (r == ARCHIVE_EOF) {
				if (detail) {
					*detail = "ordinal past the end";
				}
				return Error::NotFound;
			}
			if (r < ARCHIVE_OK && r != ARCHIVE_WARN) {
				if (detail) {
					*detail = error_text_or(handle.a,
					                        "header failed");
				}
				return Error::Corrupt;
			}
			if (index == ordinal) {
				return Error::None;
			}
			if (archive_read_data_skip(handle.a) < ARCHIVE_OK) {
				if (detail) {
					*detail = error_text_or(handle.a,
					                        "skip failed");
				}
				return Error::Corrupt;
			}
		}
	}
};

const char* ErrorText(const Error error)
{
	switch (error) {
	case Error::None: return "";
	case Error::OpenFailed: return "cannot open the archive";
	case Error::NotAnArchive: return "not a recognized archive";
	case Error::TooLarge: return "declared size over the limit";
	case Error::TooManyEntries: return "too many entries";
	case Error::Encrypted: return "password-protected archive";
	case Error::MultiVolume: return "multi-volume archive";
	case Error::SolidUnsupported:
		return "solid RAR v4 archives cannot be read";
	case Error::EntryRejected: return "entry name refused";
	case Error::Corrupt: return "archive damaged";
	case Error::Changed: return "archive changed since it was indexed";
	case Error::Unsupported: return "compression method not supported";
	case Error::NotFound: return "no such entry";
	}
	return "";
}

ArchiveReader::ArchiveReader()  = default;
ArchiveReader::~ArchiveReader() = default;

OpenResult ArchiveReader::Open(const std::filesystem::path& path, const Limits& limits)
{
	auto result     = OpenResult{};
	auto reader     = std::unique_ptr<ArchiveReader>(new ArchiveReader());
	reader->impl    = std::make_unique<Impl>();
	auto& impl      = *reader->impl;
	impl.path       = path;
	impl.limits     = limits;
	const auto fail = [&](const Error error, const std::string& detail) {
		result.error  = error;
		result.detail = detail;
		augra::log_warn(LogComponent,
		                "%s: %s (%s)",
		                path.string().c_str(),
		                ErrorText(error),
		                detail.c_str());
		return std::move(result);
	};

	impl.file.fd = open_read_only(path);
	if (impl.file.fd < 0) {
		return fail(Error::OpenFailed, "open failed");
	}
	if (!stat_fd(impl.file.fd, impl.indexed_stat)) {
		return fail(Error::OpenFailed, "fstat failed");
	}
	const auto head = read_head(impl.file.fd);
	impl.signature  = sniff_signature(head);
	if (const auto rar_flags = rar4_main_flags(head)) {
		if (*rar_flags & Rar4FlagVolume) {
			return fail(Error::MultiVolume, "RAR volume flag set");
		}
		if (*rar_flags & Rar4FlagSolid) {
			return fail(Error::SolidUnsupported, "RAR solid flag set");
		}
	}
	if (const auto rar5_flags = rar5_archive_flags(head)) {
		if (*rar5_flags & Rar5ArchiveFlagVolume) {
			return fail(Error::MultiVolume, "RAR5 volume flag set");
		}
	}

	// The zip central directory is read before the libarchive walk opens,
	// so the two never move the shared offset under each other. For any
	// other format the probe answers NotAZip and is ignored.
	const auto cd = ZipCentralDirectory::Read(impl.file.fd,
	                                          static_cast<uint64_t>(
	                                                  limits.max_entries));

	// The code page decoder drops control bytes and libarchive's zip reader
	// refuses such names only on some platforms, so the raw names are
	// checked here for one verdict on every host.
	if (impl.signature == Signature::Zip &&
	    cd.error == ZipCentralDirectory::Error::NotAZip) {
		return fail(Error::Corrupt,
		            "zip local header without an end record: truncated, or "
		            "one volume of a spanned set");
	}
	const auto control_byte_verdict = [&]() -> std::optional<std::string> {
		for (size_t i = 0; i < cd.records.size(); ++i) {
			if (has_control_bytes(cd.records[i].raw_name)) {
				return "entry " + std::to_string(i) + ": " +
				       ArchiveNames::RejectionText(
				               ArchiveNames::Rejection::ControlCharacter);
			}
		}
		return {};
	};
	if (impl.signature == Signature::Zip &&
	    cd.error == ZipCentralDirectory::Error::None) {
		if (const auto verdict = control_byte_verdict()) {
			return fail(Error::EntryRejected, *verdict);
		}
	}
	// The directory's own verdict, for a zip: at the first header or,
	// when libarchive walks nothing, after the walk.
	const auto directory_verdict =
	        [&]() -> std::optional<std::pair<Error, std::string>> {
		switch (cd.error) {
		case ZipCentralDirectory::Error::None: return {};
		case ZipCentralDirectory::Error::TooManyEntries:
			return std::pair{Error::TooManyEntries,
			                 std::string(ZipCentralDirectory::ErrorText(
			                         cd.error))};
		case ZipCentralDirectory::Error::MultiDisk:
			return std::pair{Error::MultiVolume,
			                 std::string(ZipCentralDirectory::ErrorText(
			                         cd.error))};
		case ZipCentralDirectory::Error::IoFailed:
			return std::pair{Error::OpenFailed,
			                 std::string(ZipCentralDirectory::ErrorText(
			                         cd.error))};
		default:
			return std::pair{Error::Corrupt,
			                 std::string(ZipCentralDirectory::ErrorText(
			                         cd.error))};
		}
	};

	// With a zip signature only the zip reader is registered, so the
	// directory's verdict (spanned, over the caps, past the end record)
	// goes out before libarchive bids on it or slurps the directory.
	if (impl.signature == Signature::Zip) {
		if (const auto verdict = directory_verdict()) {
			return fail(verdict->first, verdict->second);
		}
	}
	auto handle = Handle{};
	auto detail = std::string();
	switch (open_handle(impl.file.fd, impl.signature, handle, &detail)) {
	case HandleStatus::Ok: break;
	case HandleStatus::DescriptorFailed:
		return fail(Error::OpenFailed, detail);
	case HandleStatus::NotRecognized:
		return fail(Error::NotAnArchive, detail);
	}

	// zip names and CRCs come from the central directory, paired by
	// position with libarchive's walk, which goes by ascending local header
	// offset (its red-black tree); the probe refused any repeated offset.
	auto zip_records = std::vector<ZipCentralDirectory::Record>{};
	if (cd.error == ZipCentralDirectory::Error::None) {
		zip_records = cd.records;
		std::stable_sort(zip_records.begin(),
		                 zip_records.end(),
		                 [](const auto& a, const auto& b) {
			                 return a.local_header_offset <
			                        b.local_header_offset;
		                 });
	}
	auto names        = ArchiveNames::DosNameTable{};
	auto seen_paths   = std::unordered_set<std::string>{};
	auto format_known = false;
	auto walked       = 0;

	struct archive_entry* ae = nullptr;
	for (auto ordinal = 0;; ++ordinal) {
		const auto r = archive_read_next_header(handle.a, &ae);
		if (r == ARCHIVE_EOF) {
			walked = ordinal;
			break;
		}
		if (r < ARCHIVE_OK && r != ARCHIVE_WARN) {
			const auto message = std::string(error_text_or(handle.a, ""));
			if (ordinal == 0 && archive_format(handle.a) == 0) {
				return fail(Error::NotAnArchive, message);
			}
			// Both RAR readers raise the encrypted flag before they
			// fail; their messages differ in case, so the flag
			// decides.
			if (archive_read_has_encrypted_entries(handle.a) > 0) {
				return fail(Error::Encrypted, message);
			}
			if (impl.signature == Signature::Rar4 &&
			    message.find("solid") != std::string::npos) {
				return fail(Error::SolidUnsupported, message);
			}
			return fail(Error::Corrupt, message);
		}
		if (ordinal == 0) {
			const auto format = format_of(handle.a);
			if (!format) {
				return fail(Error::NotAnArchive, "unsupported format");
			}
			impl.format  = *format;
			impl.access  = access_of(*format);
			format_known = true;
			if (impl.format == Format::Zip) {
				if (const auto verdict = directory_verdict()) {
					return fail(verdict->first, verdict->second);
				}
				if (const auto verdict = control_byte_verdict()) {
					return fail(Error::EntryRejected, *verdict);
				}
			}
		}
		if (ordinal >= limits.max_entries) {
			return fail(Error::TooManyEntries,
			            "more than " + std::to_string(limits.max_entries) +
			                    " entries");
		}
		if (archive_entry_is_encrypted(ae) ||
		    archive_read_has_encrypted_entries(handle.a) > 0) {
			return fail(Error::Encrypted, "encrypted entry");
		}

		const auto filetype = archive_entry_filetype(ae);
		if (filetype != AE_IFREG && filetype != AE_IFDIR) {
			const auto* name = archive_entry_pathname_utf8(ae);
			augra::log_warn(LogComponent,
			                "%s: dropping entry %d '%s': not a file or directory",
			                path.string().c_str(),
			                ordinal,
			                name ? name : "");
			archive_read_data_skip(handle.a);
			continue;
		}

		auto utf8_name    = std::string();
		auto has_crc      = false;
		auto crc          = uint32_t{0};
		auto zip_dos_time = uint32_t{0};
		if (impl.format == Format::Zip) {
			if (static_cast<size_t>(ordinal) >= zip_records.size()) {
				return fail(Error::Corrupt,
				            "more entries than the central directory lists");
			}
			const auto& rec = zip_records[static_cast<size_t>(ordinal)];
			if (rec.encrypted) {
				return fail(Error::Encrypted, "encrypted entry");
			}
			utf8_name    = ZipCentralDirectory::DecodeName(rec);
			zip_dos_time = rec.dos_datetime;
			if (!ZipCentralDirectory::MethodReadable(
			            rec.method, probe_filters().decoders)) {
				return fail(Error::Unsupported,
				            "'" + utf8_name + "': ZIP method " +
				                    std::to_string(rec.method) + " (" +
				                    zip_method_name(rec.method) +
				                    ")");
			}
			has_crc = true;
			crc     = rec.crc32;
		} else {
			const auto* name = archive_entry_pathname_utf8(ae);
			if (!name) {
				name = archive_entry_pathname(ae);
			}
			utf8_name = name ? name : "";
		}

		auto rejection = ArchiveNames::Rejection::None;
		const auto split = ArchiveNames::SplitEntryPath(utf8_name, &rejection);
		if (!split) {
			return fail(Error::EntryRejected,
			            "'" + utf8_name + "': " +
			                    ArchiveNames::RejectionText(rejection));
		}

		// A regular file spelled as the root or as a directory would
		// lose its bytes to the directory it names.
		if (split->is_dir && filetype == AE_IFREG) {
			return fail(Error::EntryRejected,
			            "'" + utf8_name + "': a file named as a directory");
		}
		if (split->components.empty()) {
			archive_read_data_skip(handle.a);
			continue;
		}
		auto entry    = Entry{};
		entry.ordinal = ordinal;
		entry.is_dir  = filetype == AE_IFDIR || split->is_dir;
		entry.size    = entry.is_dir ? 0
		                             : (archive_entry_size_is_set(ae)
		                                        ? archive_entry_size(ae)
		                                        : -1);
		entry.mtime   = archive_entry_mtime(ae);
		entry.has_crc = has_crc && !entry.is_dir;
		entry.crc32   = crc;
		if (impl.format == Format::Zip && !entry.is_dir) {
			entry.size = static_cast<int64_t>(
			        zip_records[static_cast<size_t>(ordinal)].uncompressed_size);
		}
		if (entry.size < 0) {
			return fail(Error::TooLarge,
			            "'" + utf8_name + "': size not declared");
		}
		if (entry.size > limits.max_member_bytes) {
			return fail(Error::TooLarge,
			            "'" + utf8_name +
			                    "': " + std::to_string(entry.size) +
			                    " bytes");
		}

		// Intermediate directories appear on first sight, from an entry
		// or the first file below; a name goes only to a path not seen
		// before, so a duplicate never burns a numbered name.
		auto normalized = std::string();
		auto parent_dos = std::string();
		for (size_t i = 0; i + 1 < split->components.size(); ++i) {
			normalized += (i ? "/" : "") + split->components[i];
			if (const auto* existing = impl.at_path(normalized)) {
				if (!existing->is_dir) {
					return fail(Error::EntryRejected,
					            "'" + utf8_name + "': '" +
					                    normalized +
					                    "' is a file");
				}
				parent_dos = existing->dos_path;
				continue;
			}
			const auto dir_dos = names.Assign(parent_dos,
			                                  split->components[i]);
			if (!dir_dos) {
				return fail(Error::EntryRejected,
				            "'" + utf8_name + "': DOS path too long");
			}
			auto dir_entry      = Entry{};
			dir_entry.ordinal   = -1;
			dir_entry.utf8_path = normalized;
			dir_entry.dos_path  = *dir_dos;
			dir_entry.is_dir    = true;
			dir_entry.mtime     = entry.mtime;
			impl.add(dir_entry, 0);
			seen_paths.insert(normalized);
			parent_dos = *dir_dos;
		}
		normalized += (split->components.size() > 1 ? "/" : "") +
		              split->components.back();
		if (seen_paths.contains(normalized)) {
			// 7z and RAR list a directory after its files, so a dir
			// after a dir is expected; a file and a directory under
			// one path is a tree DOS cannot hold.
			const auto* existing = impl.at_path(normalized);
			if (existing && existing->is_dir != entry.is_dir) {
				return fail(Error::EntryRejected,
				            "'" + utf8_name + "': " +
				                    (entry.is_dir ? "a file"
				                                  : "a directory") +
				                    " of that name is already listed");
			}
			if (!entry.is_dir) {
				augra::log_warn(LogComponent,
				                "%s: duplicate entry '%s' kept once",
				                path.string().c_str(),
				                normalized.c_str());
			}
			archive_read_data_skip(handle.a);
			continue;
		}
		const auto dos_path = names.Assign(parent_dos,
		                                   split->components.back());
		if (!dos_path) {
			return fail(Error::EntryRejected,
			            "'" + utf8_name +
			                    "': no free 8.3 name or DOS path too long");
		}
		seen_paths.insert(normalized);
		entry.utf8_path = normalized;
		entry.dos_path  = *dos_path;
		impl.total_bytes += entry.size;
		if (impl.total_bytes > limits.max_total_bytes) {
			return fail(Error::TooLarge,
			            "declared total over " +
			                    std::to_string(limits.max_total_bytes) +
			                    " bytes");
		}
		impl.add(entry,
		         identity_time_of(impl.format, entry.mtime, zip_dos_time));
		if (archive_read_data_skip(handle.a) < ARCHIVE_OK) {
			return fail(Error::Corrupt,
			            error_text_or(handle.a, "skip failed"));
		}
	}
	if (!format_known) {
		if (impl.signature == Signature::Zip) {
			if (const auto verdict = directory_verdict()) {
				return fail(verdict->first, verdict->second);
			}
		}
		const auto format = format_of(handle.a);
		if (!format) {
			return fail(Error::NotAnArchive, "no entries and no format");
		}
		impl.format = *format;
		impl.access = access_of(*format);
	}
	if (impl.format == Format::Zip) {
		if (const auto verdict = directory_verdict()) {
			return fail(verdict->first, verdict->second);
		}
	}
	if (impl.format == Format::Zip &&
	    static_cast<size_t>(walked) != zip_records.size()) {
		return fail(Error::Corrupt,
		            "central directory lists " +
		                    std::to_string(zip_records.size()) +
		                    " entries, libarchive walked " +
		                    std::to_string(walked));
	}

	auto listing = std::vector<ArchiveIdentity::ListingEntry>{};
	for (size_t i = 0; i < impl.entries.size(); ++i) {
		const auto& e = impl.entries[i];
		if (!e.is_dir) {
			listing.push_back({e.utf8_path,
			                   e.size,
			                   impl.identity_times[i],
			                   e.has_crc,
			                   e.crc32});
		}
	}
	impl.identity = ArchiveIdentity::Hash(listing, impl.indexed_stat.size);
	result.reader = std::move(reader);
	return result;
}

Format ArchiveReader::GetFormat() const
{
	return impl->format;
}
Access ArchiveReader::GetAccess() const
{
	return impl->access;
}
const std::vector<Entry>& ArchiveReader::Entries() const
{
	return impl->entries;
}
int64_t ArchiveReader::TotalBytes() const
{
	return impl->total_bytes;
}
int64_t ArchiveReader::ArchiveLength() const
{
	return impl->indexed_stat.size;
}
const std::string& ArchiveReader::IdentityHash() const
{
	return impl->identity;
}

Error ArchiveReader::Materialize(const int ordinal, std::vector<uint8_t>& out,
                                 std::string* detail)
{
	out.clear();
	const auto use = Impl::Use(impl->in_use);
	if (const auto changed = impl->check_unchanged(detail);
	    changed != Error::None) {
		return changed;
	}
	const auto* entry = impl->find(ordinal);
	if (!entry || entry->is_dir) {
		if (detail) {
			*detail = "no such member";
		}
		return Error::NotFound;
	}
	auto handle = Handle{};
	if (const auto seek = impl->seek_to(handle, ordinal, detail);
	    seek != Error::None) {
		return seek;
	}
	return impl->read_member(
	        handle.a,
	        *entry,
	        [&](const uint8_t* data, const size_t size) {
		        out.insert(out.end(), data, data + size);
		        return true;
	        },
	        detail);
}

Error ArchiveReader::MaterializeToFile(const int ordinal,
                                       const std::filesystem::path& target,
                                       std::string* detail)
{
	const auto use = Impl::Use(impl->in_use);
	if (const auto changed = impl->check_unchanged(detail);
	    changed != Error::None) {
		return changed;
	}
	const auto* entry = impl->find(ordinal);
	if (!entry || entry->is_dir) {
		if (detail) {
			*detail = "no such member";
		}
		return Error::NotFound;
	}
	auto handle = Handle{};
	if (const auto seek = impl->seek_to(handle, ordinal, detail);
	    seek != Error::None) {
		return seek;
	}
	return impl->write_member(handle.a, *entry, target, detail);
}

Error ArchiveReader::MaterializeAll(const std::filesystem::path& directory,
                                    std::string* detail)
{
	const auto use = Impl::Use(impl->in_use);
	if (const auto changed = impl->check_unchanged(detail);
	    changed != Error::None) {
		return changed;
	}
	std::error_code ec = {};
	std::filesystem::create_directories(directory, ec);
	if (ec) {
		if (detail) {
			*detail = "cannot create " + directory.string();
		}
		return Error::OpenFailed;
	}
	auto handle = Handle{};
	if (open_handle(impl->file.fd, impl->signature, handle, detail) !=
	    HandleStatus::Ok) {
		return Error::OpenFailed;
	}
	struct archive_entry* ae = nullptr;
	for (auto ordinal = 0;; ++ordinal) {
		const auto r = archive_read_next_header(handle.a, &ae);
		if (r == ARCHIVE_EOF) {
			return Error::None;
		}
		if (r < ARCHIVE_OK && r != ARCHIVE_WARN) {
			if (detail) {
				*detail = error_text_or(handle.a, "header failed");
			}
			return Error::Corrupt;
		}
		const auto* entry = impl->find(ordinal);
		if (!entry || entry->is_dir) {
			archive_read_data_skip(handle.a);
			continue;
		}
		const auto written = impl->write_member(
		        handle.a, *entry, directory / std::to_string(ordinal), detail);
		if (written != Error::None) {
			return written;
		}
	}
}

} // namespace ArchiveMount
