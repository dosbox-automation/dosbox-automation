// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#ifndef DOSBOX_TESTS_ARCHIVE_FIXTURES_H
#define DOSBOX_TESTS_ARCHIVE_FIXTURES_H

#include <archive.h>
#include <archive_entry.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace ArchiveFixtures {

struct StoredEntry {
	std::string name    = {};
	std::string data    = {};
	bool utf8_flag      = false;
	bool encrypted_flag = false;
	// When set, both headers declare this uncompressed size instead of
	// data.size() (the declared-size bomb of the design's threat list);
	// the local header carries the zip64 sentinel above 4 GiB.
	std::optional<uint64_t> declared_size = {};
	// The method written into both headers; the data stays as is,
	// so anything but 0 is a header-only lie for index-time tests.
	uint16_t method = 0;
	// The local header offset the central directory claims, when it is
	// to lie (two records at one offset).
	std::optional<uint64_t> central_offset = {};
	// What the local header says when it disagrees with the directory.
	std::optional<uint32_t> local_crc    = {};
	std::optional<uint16_t> local_method = {};
	std::optional<uint32_t> local_csize  = {};
	std::optional<uint32_t> local_usize  = {};
};

struct ZipOptions {
	std::string comment = {};
	bool zip64          = false;
	// Central directory record order as entry indices; empty keeps the
	// local order. APPNOTE requires no order, repair tools sort.
	std::vector<size_t> central_order = {};
	std::string prefix = {}; // bytes before the zip (SFX stub)
	std::string suffix = {}; // bytes after the end record
	// With zip64: whether the end record's fields sit at their sentinels
	// (Info-ZIP) or carry the real values (libarchive's writer, zip -fz).
	bool zip64_eocd_sentinels = true;
};

inline void put16(std::vector<uint8_t>& out, const uint32_t v)
{
	out.push_back(static_cast<uint8_t>(v & 0xff));
	out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
}
inline void put32(std::vector<uint8_t>& out, const uint32_t v)
{
	put16(out, v & 0xffff);
	put16(out, (v >> 16) & 0xffff);
}
inline void put64(std::vector<uint8_t>& out, const uint64_t v)
{
	put32(out, static_cast<uint32_t>(v & 0xffffffffu));
	put32(out, static_cast<uint32_t>(v >> 32));
}

// CRC-32 as PKZIP computes it (IEEE 802.3 polynomial, reflected).
inline uint32_t Crc32(const std::string& data)
{
	uint32_t crc = 0xffffffffu;
	for (const auto c : data) {
		crc ^= static_cast<uint8_t>(c);
		for (auto bit = 0; bit < 8; ++bit) {
			crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
		}
	}
	return ~crc;
}

// A stored (method 0) zip with the given entries, a comment, and, when
// zip64 is requested, the zip64 end records with the 32-bit fields at
// their 0xFFFF / 0xFFFFFFFF sentinels (APPNOTE 4.3.14-4.3.16).
inline std::vector<uint8_t> MakeStoredZip(const std::vector<StoredEntry>& entries,
                                          const ZipOptions& options)
{
	const auto& comment           = options.comment;
	const auto zip64              = options.zip64;
	std::vector<uint8_t> out      = {};
	std::vector<uint64_t> offsets = {};
	for (const auto& e : entries) {
		offsets.push_back(out.size());
		const uint16_t flags = static_cast<uint16_t>(
		        (e.utf8_flag ? 0x800 : 0) | (e.encrypted_flag ? 0x1 : 0));
		put32(out, 0x04034b50u);
		put16(out, 20);
		put16(out, flags);
		put16(out, e.local_method.value_or(e.method));
		put16(out, 0);
		put16(out, 0x21); // time, date (1980-01-01)
		put32(out, e.local_crc.value_or(Crc32(e.data)));
		put32(out,
		      e.local_csize.value_or(static_cast<uint32_t>(e.data.size())));
		const uint64_t local_usize = e.declared_size.value_or(e.data.size());
		put32(out,
		      e.local_usize.value_or(
		              local_usize > 0xffffffffu
		                      ? 0xffffffffu
		                      : static_cast<uint32_t>(local_usize)));
		put16(out, static_cast<uint32_t>(e.name.size()));
		put16(out, 0);
		out.insert(out.end(), e.name.begin(), e.name.end());
		out.insert(out.end(), e.data.begin(), e.data.end());
	}
	const auto cd_offset = out.size();
	auto order           = options.central_order;
	if (order.empty()) {
		for (size_t i = 0; i < entries.size(); ++i) {
			order.push_back(i);
		}
	}
	for (const auto i : order) {
		const auto& e        = entries[i];
		const uint16_t flags = static_cast<uint16_t>(
		        (e.utf8_flag ? 0x800 : 0) | (e.encrypted_flag ? 0x1 : 0));
		const uint64_t usize = e.declared_size.value_or(e.data.size());
		const bool needs_zip64_extra = zip64 || usize > 0xffffffffu;
		put32(out, 0x02014b50u);
		put16(out, 20);
		put16(out, 20);
		put16(out, flags);
		put16(out, e.method);
		put16(out, 0);
		put16(out, 0x21);
		put32(out, Crc32(e.data));
		put32(out,
		      needs_zip64_extra ? 0xffffffffu
		                        : static_cast<uint32_t>(e.data.size()));
		put32(out,
		      needs_zip64_extra ? 0xffffffffu : static_cast<uint32_t>(usize));
		put16(out, static_cast<uint32_t>(e.name.size()));
		put16(out, needs_zip64_extra ? 28 : 0); // extra length
		put16(out, 0);                          // comment length
		put16(out, 0);                          // disk
		put16(out, 0);                          // internal attributes
		put32(out, 0);                          // external attributes
		const auto claimed = e.central_offset.value_or(offsets[i]);
		put32(out,
		      needs_zip64_extra ? 0xffffffffu
		                        : static_cast<uint32_t>(claimed));
		out.insert(out.end(), e.name.begin(), e.name.end());
		if (needs_zip64_extra) {
			put16(out, 0x0001);
			put16(out, 24);
			put64(out, usize);
			put64(out, e.data.size());
			put64(out, claimed);
		}
	}
	const auto cd_size = out.size() - cd_offset;
	if (zip64) {
		const auto zip64_eocd_offset = out.size();
		put32(out, 0x06064b50u);
		put64(out, 44); // size of the rest of this record
		put16(out, 45);
		put16(out, 45);
		put32(out, 0);
		put32(out, 0);
		put64(out, entries.size());
		put64(out, entries.size());
		put64(out, cd_size);
		put64(out, cd_offset);
		put32(out, 0x07064b50u);
		put32(out, 0);
		put64(out, zip64_eocd_offset);
		put32(out, 1);
	}
	const auto sentinels = zip64 && options.zip64_eocd_sentinels;
	put32(out, 0x06054b50u);
	put16(out, 0);
	put16(out, 0);
	put16(out, sentinels ? 0xffff : static_cast<uint32_t>(entries.size()));
	put16(out, sentinels ? 0xffff : static_cast<uint32_t>(entries.size()));
	put32(out, sentinels ? 0xffffffffu : static_cast<uint32_t>(cd_size));
	put32(out, sentinels ? 0xffffffffu : static_cast<uint32_t>(cd_offset));
	put16(out, static_cast<uint32_t>(comment.size()));
	out.insert(out.end(), comment.begin(), comment.end());
	out.insert(out.end(), options.suffix.begin(), options.suffix.end());
	out.insert(out.begin(), options.prefix.begin(), options.prefix.end());
	return out;
}

inline std::vector<uint8_t> MakeStoredZip(const std::vector<StoredEntry>& entries,
                                          const std::string& comment = "",
                                          const bool zip64           = false)
{
	auto options    = ZipOptions{};
	options.comment = comment;
	options.zip64   = zip64;
	return MakeStoredZip(entries, options);
}

inline void WriteBytes(const std::filesystem::path& path,
                       const std::vector<uint8_t>& bytes)
{
	auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
	out.write(reinterpret_cast<const char*>(bytes.data()),
	          static_cast<std::streamsize>(bytes.size()));
}

struct FileEntry {
	std::string name = {};
	std::string data = {};
	bool is_dir      = false;
	bool is_symlink  = false;
	int64_t mtime    = 631152000; // 1990-01-01
};

enum class Kind { ZipStored, ZipDeflate, SevenZip, Tar, TarGz, TarXz, TarZst };

// Writes an archive with libarchive's writer. Returns false when the
// writer refuses something (the test then fails with the reason).
inline bool WriteArchive(const std::filesystem::path& path, const Kind kind,
                         const std::vector<FileEntry>& entries,
                         const char* passphrase = nullptr,
                         std::string* error     = nullptr)
{
	struct archive* a = archive_write_new();
	int r             = ARCHIVE_OK;
	switch (kind) {
	case Kind::ZipStored:
		r = archive_write_set_format_zip(a);
		if (r == ARCHIVE_OK) {
			r = archive_write_set_format_option(a, "zip", "compression", "store");
		}
		break;
	case Kind::ZipDeflate: r = archive_write_set_format_zip(a); break;
	case Kind::SevenZip: r = archive_write_set_format_7zip(a); break;
	case Kind::Tar: r = archive_write_set_format_pax_restricted(a); break;
	case Kind::TarGz:
		r = archive_write_set_format_pax_restricted(a);
		if (r == ARCHIVE_OK) {
			r = archive_write_add_filter_gzip(a);
		}
		break;
	case Kind::TarXz:
		r = archive_write_set_format_pax_restricted(a);
		if (r == ARCHIVE_OK) {
			r = archive_write_add_filter_xz(a);
		}
		break;
	case Kind::TarZst:
		r = archive_write_set_format_pax_restricted(a);
		if (r == ARCHIVE_OK) {
			r = archive_write_add_filter_zstd(a);
		}
		break;
	}
	if (r == ARCHIVE_OK && passphrase) {
		r = archive_write_set_options(a, "zip:encryption=zipcrypt");
		if (r == ARCHIVE_OK) {
			r = archive_write_set_passphrase(a, passphrase);
		}
	}
	if (r == ARCHIVE_OK) {
		r = archive_write_open_filename(a, path.string().c_str());
	}
	for (const auto& e : entries) {
		if (r != ARCHIVE_OK) {
			break;
		}
		struct archive_entry* ae = archive_entry_new();
		archive_entry_set_pathname(ae, e.name.c_str());
		archive_entry_set_mtime(ae, e.mtime, 0);
		if (e.is_symlink) {
			archive_entry_set_filetype(ae, AE_IFLNK);
			archive_entry_set_symlink(ae, e.data.c_str());
			archive_entry_set_perm(ae, 0777);
		} else if (e.is_dir) {
			archive_entry_set_filetype(ae, AE_IFDIR);
			archive_entry_set_perm(ae, 0755);
		} else {
			archive_entry_set_filetype(ae, AE_IFREG);
			archive_entry_set_perm(ae, 0644);
			archive_entry_set_size(ae,
			                       static_cast<la_int64_t>(e.data.size()));
		}
		r = archive_write_header(a, ae);
		if (r == ARCHIVE_OK && !e.is_dir && !e.is_symlink &&
		    !e.data.empty()) {
			const auto written = archive_write_data(a,
			                                        e.data.data(),
			                                        e.data.size());
			if (written != static_cast<la_ssize_t>(e.data.size())) {
				r = ARCHIVE_FATAL;
			}
		}
		archive_entry_free(ae);
	}
	if (error && r != ARCHIVE_OK) {
		*error = archive_error_string(a) ? archive_error_string(a)
		                                 : "unknown";
	}
	const auto close_r = archive_write_close(a);
	archive_write_free(a);
	return r == ARCHIVE_OK && close_r == ARCHIVE_OK;
}

} // namespace ArchiveFixtures

#endif // DOSBOX_TESTS_ARCHIVE_FIXTURES_H
