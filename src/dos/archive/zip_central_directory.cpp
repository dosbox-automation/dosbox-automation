// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/archive/zip_central_directory.h"

#include "dos/archive/descriptor.h"

#include "misc/cross.h"
#include "misc/unicode.h"
#include "utils/checks.h"

#include <algorithm>
#include <cstdio>
#include <fcntl.h>
#include <system_error>

#if defined(WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

CHECK_NARROWING();

// Format reference: PKWARE APPNOTE.TXT 6.3.10, sections 4.3.12 (central
// directory header), 4.3.14 to 4.3.16 (zip64 end records, end record),
// 4.4.4 (general purpose bit flag), 4.5.3 (zip64 extra field).

namespace ZipCentralDirectory {

namespace {

constexpr uint32_t EndRecordSignature      = 0x06054b50u;
constexpr uint32_t Zip64EndRecordSignature = 0x06064b50u;
constexpr uint32_t Zip64LocatorSignature   = 0x07064b50u;
constexpr uint32_t CentralHeaderSignature  = 0x02014b50u;
constexpr size_t EndRecordSize             = 22;
constexpr size_t Zip64LocatorSize          = 20;
constexpr size_t Zip64EndRecordSize        = 56;
constexpr uint64_t Zip64EndRecordMaxSize   = 16384;
constexpr size_t CentralHeaderSize         = 46;
constexpr size_t LocalHeaderSize           = 30;
constexpr uint32_t LocalHeaderSignature    = 0x04034b50u;
constexpr uint64_t LibarchiveSearchWindow  = 16 * 1024;
constexpr uint16_t Zip64ExtraId            = 0x0001;
constexpr uint16_t FlagEncrypted           = 0x0001;
constexpr uint16_t FlagStrongEncryption    = 0x0040;
constexpr uint16_t FlagUtf8                = 0x0800;

uint16_t le16(const uint8_t* p)
{
	return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t le32(const uint8_t* p)
{
	return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
	       (static_cast<uint32_t>(p[2]) << 16) |
	       (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t le64(const uint8_t* p)
{
	return static_cast<uint64_t>(le32(p)) |
	       (static_cast<uint64_t>(le32(p + 4)) << 32);
}

// The zip64 extended information extra field (APPNOTE 4.5.3) carries
// only the fields whose 32-bit form is at its sentinel, in this order:
// uncompressed size, compressed size, local header offset, disk.
bool apply_zip64_extra(std::span<const uint8_t> extra, Record& record,
                       bool& disk_sentinel)
{
	size_t pos = 0;
	while (pos + 4 <= extra.size()) {
		const auto id   = le16(extra.data() + pos);
		const auto size = le16(extra.data() + pos + 2);
		pos += 4;
		if (pos + size > extra.size()) {
			return false;
		}
		if (id == Zip64ExtraId) {
			size_t field    = pos;
			const auto take = [&](uint64_t& out) {
				if (field + 8 > pos + size) {
					return false;
				}
				out = le64(extra.data() + field);
				field += 8;
				return true;
			};
			if (record.uncompressed_size == 0xffffffffu &&
			    !take(record.uncompressed_size)) {
				return false;
			}
			if (record.compressed_size == 0xffffffffu &&
			    !take(record.compressed_size)) {
				return false;
			}
			if (record.local_header_offset == 0xffffffffu &&
			    !take(record.local_header_offset)) {
				return false;
			}
			if (disk_sentinel) {
				if (field + 4 > pos + size) {
					return false;
				}
				disk_sentinel = le32(extra.data() + field) != 0;
			}
			return true;
		}
		pos += size;
	}
	return true;
}

struct File {
	FILE* handle = nullptr;
	~File()
	{
		if (handle) {
			fclose(handle);
		}
	}
};

bool read_at(FILE* f, const uint64_t offset, std::vector<uint8_t>& out,
             const size_t size)
{
	out.resize(size);
	if (cross_fseeko(f, static_cast<cross_off_t>(offset), SEEK_SET) != 0) {
		return false;
	}
	return fread(out.data(), 1, size, f) == size;
}

} // namespace

const char* ErrorText(const Error error)
{
	switch (error) {
	case Error::None: return "";
	case Error::NotAZip: return "no zip end record";
	case Error::Truncated: return "zip central directory truncated";
	case Error::Malformed: return "zip central directory malformed";
	case Error::MultiDisk: return "spanned zip archive";
	case Error::TooManyEntries: return "too many zip entries";
	case Error::IoFailed: return "zip could not be read";
	case Error::LocalHeaderMismatch:
		return "a zip local header disagrees with the central directory";
	case Error::DuplicateOffset:
		return "two zip records at one local header offset";
	}
	return "";
}

Result Parse(const std::span<const uint8_t> central_directory,
             const uint64_t entry_count, const uint64_t max_entries)
{
	auto result = Result{};
	if (entry_count > max_entries) {
		result.error = Error::TooManyEntries;
		return result;
	}
	size_t pos = 0;
	for (uint64_t i = 0; i < entry_count; ++i) {
		if (pos + CentralHeaderSize > central_directory.size()) {
			result.error = Error::Truncated;
			return result;
		}
		const auto* p = central_directory.data() + pos;
		if (le32(p) != CentralHeaderSignature) {
			result.error = Error::Malformed;
			return result;
		}
		auto record              = Record{};
		const auto flags         = le16(p + 8);
		record.utf8_flag         = (flags & FlagUtf8) != 0;
		record.encrypted         = (flags &
                                    (FlagEncrypted | FlagStrongEncryption)) != 0;
		record.method            = le16(p + 10);
		record.dos_datetime      = le32(p + 12);
		record.crc32             = le32(p + 16);
		record.compressed_size   = le32(p + 20);
		record.uncompressed_size = le32(p + 24);
		const size_t name_len    = le16(p + 28);
		const size_t extra_len   = le16(p + 30);
		const size_t comment_len = le16(p + 32);
		auto disk_sentinel       = le16(p + 34) == 0xffff;
		if (!disk_sentinel && le16(p + 34) != 0) {
			result.error = Error::MultiDisk;
			return result;
		}
		record.external_attributes = le32(p + 38);
		record.local_header_offset = le32(p + 42);
		const auto record_end = pos + CentralHeaderSize + name_len +
		                        extra_len + comment_len;
		if (record_end > central_directory.size()) {
			result.error = Error::Truncated;
			return result;
		}
		record.raw_name.assign(reinterpret_cast<const char*>(
		                               p + CentralHeaderSize),
		                       name_len);
		const auto extra = central_directory.subspan(pos + CentralHeaderSize +
		                                                     name_len,
		                                             extra_len);
		if (!apply_zip64_extra(extra, record, disk_sentinel)) {
			result.error = Error::Malformed;
			return result;
		}
		if (disk_sentinel) {
			result.error = Error::MultiDisk;
			return result;
		}
		result.records.push_back(std::move(record));
		pos = record_end;
	}
	// libarchive walks the directory to its end record, not by count.
	if (pos != central_directory.size()) {
		result.error = Error::Malformed;
		return result;
	}
	return result;
}

// libarchive decodes by the local header and only warns when its CRC or
// size disagrees with the directory, so the bytes would pass under the
// directory's name. A 0 (or 0xffffffff) local field defers, as libarchive's does.
static Error check_local_headers(File& file, const std::vector<Record>& records,
                                 const uint64_t file_size)
{
	std::vector<uint8_t> local = {};
	for (const auto& record : records) {
		if (record.local_header_offset + LocalHeaderSize > file_size) {
			return Error::Truncated;
		}
		if (!read_at(file.handle, record.local_header_offset, local, LocalHeaderSize)) {
			return Error::IoFailed;
		}
		if (le32(local.data()) != LocalHeaderSignature ||
		    le16(local.data() + 8) != record.method) {
			return Error::LocalHeaderMismatch;
		}
		const auto crc   = le32(local.data() + 14);
		const auto csize = le32(local.data() + 18);
		const auto usize = le32(local.data() + 22);
		if (crc != 0 && crc != record.crc32) {
			return Error::LocalHeaderMismatch;
		}
		if (csize != 0 && csize != 0xffffffffu &&
		    csize != record.compressed_size) {
			return Error::LocalHeaderMismatch;
		}
		if (usize != 0 && usize != 0xffffffffu &&
		    usize != record.uncompressed_size) {
			return Error::LocalHeaderMismatch;
		}
	}
	return Error::None;
}

static Result read_from(File& file, const uint64_t max_entries)
{
	auto result = Result{};
	if (cross_fseeko(file.handle, 0, SEEK_END) != 0) {
		result.error = Error::NotAZip;
		return result;
	}
	const auto file_size = static_cast<uint64_t>(cross_ftello(file.handle));
	if (file_size < EndRecordSize) {
		result.error = Error::NotAZip;
		return result;
	}

	// libarchive's rules (archive_read_format_zip_seekable_bid): the last
	// signature in the final 16 KiB, never at the window's first byte,
	// comment length unchecked; any other rule indexes what it won't walk.
	const auto tail_size = static_cast<size_t>(
	        std::min<uint64_t>(file_size, LibarchiveSearchWindow));
	std::vector<uint8_t> tail = {};
	if (!read_at(file.handle, file_size - tail_size, tail, tail_size)) {
		result.error = Error::IoFailed;
		return result;
	}
	size_t eocd = tail_size;
	for (size_t i = tail_size - EndRecordSize; i > 0; --i) {
		if (le32(tail.data() + i) == EndRecordSignature) {
			eocd = i;
			break;
		}
	}
	if (eocd == tail_size) {
		result.error = Error::NotAZip;
		return result;
	}
	const auto* e               = tail.data() + eocd;
	uint64_t entry_count        = le16(e + 10);
	uint64_t cd_size            = le32(e + 12);
	uint64_t cd_offset          = le32(e + 16);
	const auto eocd_file_offset = file_size - tail_size + eocd;
	// zip64 goes by the locator before the end record, as libarchive's
	// read_zip64_eocd does; a valid zip64 record wins over the end record.
	// Prepended bytes shift a plain zip's offsets by the correction below.
	auto cd_start = uint64_t{0};
	auto zip64_ok = false;
	if (eocd >= Zip64LocatorSize) {
		std::vector<uint8_t> locator = {};
		if (read_at(file.handle,
		            eocd_file_offset - Zip64LocatorSize,
		            locator,
		            Zip64LocatorSize) &&
		    le32(locator.data()) == Zip64LocatorSignature &&
		    le32(locator.data() + 4) == 0 && le32(locator.data() + 16) == 1) {
			const auto zip64_offset  = le64(locator.data() + 8);
			std::vector<uint8_t> z64 = {};
			// the record's own size field, 56 to 16384 with the
			// 12-byte head, all of it readable; the signature is
			// not checked
			if (zip64_offset + Zip64EndRecordSize <= file_size &&
			    read_at(file.handle, zip64_offset, z64, Zip64EndRecordSize)) {
				const auto record_size = le64(z64.data() + 4) + 12;
				if (record_size >= Zip64EndRecordSize &&
				    record_size <= Zip64EndRecordMaxSize &&
				    zip64_offset + record_size <= file_size &&
				    le32(z64.data() + 16) == 0 &&
				    le32(z64.data() + 20) == 0 &&
				    le64(z64.data() + 24) == le64(z64.data() + 32)) {
					entry_count = le64(z64.data() + 32);
					cd_size     = le64(z64.data() + 40);
					cd_offset   = le64(z64.data() + 48);
					cd_start    = cd_offset;
					zip64_ok    = true;
				}
			}
		}
	}
	if (!zip64_ok) {
		if (entry_count == 0xffff || cd_size == 0xffffffffu ||
		    cd_offset == 0xffffffffu) {
			result.error = Error::Malformed;
			return result;
		}
		if (le16(e + 4) != 0 || le16(e + 6) != 0 ||
		    le16(e + 8) != le16(e + 10)) {
			result.error = Error::MultiDisk;
			return result;
		}
		if (cd_offset + cd_size > eocd_file_offset) {
			result.error = Error::Truncated;
			return result;
		}
		cd_start = eocd_file_offset - cd_size;
	}

	if (entry_count > max_entries) {
		result.error = Error::TooManyEntries;
		return result;
	}
	if (cd_size > MaxDirectoryBytes || cd_start > file_size ||
	    cd_size > file_size - cd_start) {
		result.error = Error::Truncated;
		return result;
	}
	std::vector<uint8_t> directory = {};
	if (!read_at(file.handle, cd_start, directory, static_cast<size_t>(cd_size))) {
		result.error = Error::Truncated;
		return result;
	}
	result = Parse(directory, entry_count, max_entries);
	if (result.error != Error::None) {
		return result;
	}
	if (cd_start != cd_offset) {
		const auto correction = cd_start - cd_offset;
		for (auto& record : result.records) {
			record.local_header_offset += correction;
		}
	}
	// libarchive keeps one entry per offset and drops the rest silently
	auto offsets = std::vector<uint64_t>{};
	for (const auto& record : result.records) {
		offsets.push_back(record.local_header_offset);
	}
	std::sort(offsets.begin(), offsets.end());
	if (std::adjacent_find(offsets.begin(), offsets.end()) != offsets.end()) {
		result.error = Error::DuplicateOffset;
		return result;
	}
	result.error = check_local_headers(file, result.records, file_size);
	return result;
}

Result Read(const std::filesystem::path& path, const uint64_t max_entries)
{
	auto file = File{};
#if defined(WIN32)
	const auto fd = _wopen(path.wstring().c_str(),
	                       _O_RDONLY | _O_BINARY | _O_NOINHERIT);
	file.handle   = fd >= 0 ? _fdopen(fd, "rb") : nullptr;
	if (!file.handle && fd >= 0) {
		_close(fd);
	}
#else
	const auto fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
	file.handle   = fd >= 0 ? fdopen(fd, "rb") : nullptr;
	if (!file.handle && fd >= 0) {
		close(fd);
	}
#endif
	if (!file.handle) {
		auto result  = Result{};
		result.error = Error::IoFailed;
		return result;
	}
	return read_from(file, max_entries);
}

// A duplicate shares the open file description, offset included, so
// the caller's position is saved and put back after the read.
Result Read(const int fd, const uint64_t max_entries)
{
	auto file = File{};
#if defined(WIN32)
	const auto caller_offset = _telli64(fd);
	const auto dup_fd        = ArchiveMount::DuplicateNoInherit(fd);
	file.handle = dup_fd >= 0 ? _fdopen(dup_fd, "rb") : nullptr;
	if (!file.handle && dup_fd >= 0) {
		_close(dup_fd);
	}
#else
	const auto caller_offset = lseek(fd, 0, SEEK_CUR);
	const auto dup_fd        = ArchiveMount::DuplicateNoInherit(fd);
	file.handle              = dup_fd >= 0 ? fdopen(dup_fd, "rb") : nullptr;
	if (!file.handle && dup_fd >= 0) {
		close(dup_fd);
	}
#endif
	if (!file.handle) {
		auto result  = Result{};
		result.error = Error::IoFailed;
		return result;
	}
	auto result = read_from(file, max_entries);
	// POSIX lets fclose move the shared offset to the stream position, so
	// the FILE goes first and the caller's position last.
	fclose(file.handle);
	file.handle = nullptr;
	if (caller_offset >= 0) {
#if defined(WIN32)
		_lseeki64(fd, caller_offset, SEEK_SET);
#else
		lseek(fd, caller_offset, SEEK_SET);
#endif
	}
	return result;
}

bool MethodReadable(const uint16_t method, const Decoders& decoders)
{
	switch (method) {
	case 0:
	case 8:
	case 98: return true;
	case 12: return decoders.bzip2;
	case 14:
	case 95: return decoders.xz;
	case 93: return decoders.zstd;
	default: return false;
	}
}

std::string DecodeName(const Record& record)
{
	if (record.utf8_flag) {
		return record.raw_name;
	}
	return dos_437_to_fs_utf8(record.raw_name);
}

} // namespace ZipCentralDirectory
