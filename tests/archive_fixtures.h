// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#ifndef DOSBOX_TESTS_ARCHIVE_FIXTURES_H
#define DOSBOX_TESTS_ARCHIVE_FIXTURES_H

#include <archive.h>
#include <archive_entry.h>
#include <bzlib.h>
#include <zlib.h>

#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <cassert>
#include <cctype>
#include <clocale>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
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
	// General purpose bits beyond utf8 and encryption (implode's 1 and 2).
	uint16_t extra_flags = 0;
	// What the local header says when it disagrees with the directory.
	std::optional<uint16_t> local_flags  = {};
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

// libarchive's narrow opener takes the ANSI code page on Windows, where a
// name outside it (katakana in the member path tests) has no mapping.
inline int OpenWriterAt(struct archive* a, const std::filesystem::path& path)
{
#if defined(WIN32)
	return archive_write_open_filename_w(a, path.c_str());
#else
	return archive_write_open_filename(a, path.c_str());
#endif
}

// The mtime through stat: MSVC's file_clock has no to_sys (2026-10-04).
inline int64_t MtimeOf(const std::filesystem::path& path)
{
#if defined(WIN32)
	struct _stat64 st = {};
	_wstat64(path.wstring().c_str(), &st);
#else
	struct stat st = {};
	stat(path.c_str(), &st);
#endif
	return static_cast<int64_t>(st.st_mtime);
}

struct FileEntry {
	std::string name = {};
	std::string data = {};
	bool is_dir      = false;
	bool is_symlink  = false;
	int64_t mtime    = 631152000; // 1990-01-01
};

inline std::string ReadAll(const std::filesystem::path& path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), {});
}

// The smallest ISO 9660 volume isoDrive mounts: a primary volume descriptor
// (ECMA-119 8.4), the set terminator (8.3) and an empty root directory.
inline std::string MinimalIso()
{
	constexpr size_t Sector = 2048;
	std::string iso(20 * Sector, '\0');
	const auto le32 = [&](const size_t at, const uint32_t v) {
		for (int i = 0; i < 4; ++i) {
			iso[at + i] = static_cast<char>(v >> (8 * i));
		}
	};
	const auto be32 = [&](const size_t at, const uint32_t v) {
		for (int i = 0; i < 4; ++i) {
			iso[at + 3 - i] = static_cast<char>(v >> (8 * i));
		}
	};
	// A directory record (ECMA-119 9.1) for the root extent at sector 18.
	const auto dir_record = [&](const size_t at, const char ident) {
		iso[at] = 34;
		le32(at + 2, 18);
		be32(at + 6, 18);
		le32(at + 10, Sector);
		be32(at + 14, Sector);
		iso[at + 25] = 2; // a directory
		iso[at + 28] = 1; // volume sequence number, both byte orders
		iso[at + 31] = 1;
		iso[at + 32] = 1;
		iso[at + 33] = ident;
	};
	const size_t pvd = 16 * Sector;
	iso[pvd]         = 1;
	iso.replace(pvd + 1, 5, "CD001");
	iso[pvd + 6] = 1;
	iso.replace(pvd + 40, 32, "TEST                            ");
	le32(pvd + 80, 20); // volume space size, both byte orders
	be32(pvd + 84, 20);
	iso[pvd + 120] = 1; // volume set size
	iso[pvd + 123] = 1;
	iso[pvd + 124] = 1; // volume sequence number
	iso[pvd + 127] = 1;
	iso[pvd + 129] = 8; // logical block size 2048, both byte orders
	iso[pvd + 130] = 8;
	dir_record(pvd + 156, '\0');
	const size_t terminator = 17 * Sector;
	iso[terminator]         = static_cast<char>(255);
	iso.replace(terminator + 1, 5, "CD001");
	iso[terminator + 6] = 1;
	const size_t root   = 18 * Sector;
	dir_record(root, '\0');
	dir_record(root + 34, '\1');
	return iso;
}

// An ISO 9660 volume with files at the root or one directory down, as
// level 1 names with the ";1" version (ECMA-119 7.5). libarchive's isPVD
// wants what MinimalIso lacks: path tables, version byte, over 20 blocks.
inline std::string IsoWithFiles(const std::vector<FileEntry>& files)
{
	constexpr size_t Sector = 2048;
	struct Dir {
		std::string name          = {}; // "" for the root
		std::vector<size_t> files = {};
		size_t extent             = 0;
	};
	std::vector<Dir> dirs = {Dir{}};
	for (size_t i = 0; i < files.size(); ++i) {
		const auto& path = files[i].name;
		const auto slash = path.find('/');
		if (slash == std::string::npos) {
			dirs[0].files.push_back(i);
			continue;
		}
		const auto dir = path.substr(0, slash);
		auto it = std::find_if(dirs.begin(), dirs.end(), [&](const Dir& d) {
			return d.name == dir;
		});
		if (it == dirs.end()) {
			dirs.push_back(Dir{dir, {}, 0});
			it = dirs.end() - 1;
		}
		it->files.push_back(i);
	}
	// 16 PVD, 17 terminator, 18 and 19 the L and M path tables, 20.. one
	// sector per directory, then one extent per file in whole sectors.
	constexpr size_t PathTableL = 18;
	constexpr size_t PathTableM = 19;
	size_t next                 = 20;
	for (auto& d : dirs) {
		d.extent = next++;
	}
	std::vector<size_t> file_extent(files.size(), 0);
	for (size_t i = 0; i < files.size(); ++i) {
		file_extent[i] = next;
		next += (files[i].data.size() + Sector - 1) / Sector;
	}
	// libarchive's iso9660 bidder reads 48 KiB (the system area plus eight
	// blocks) before it looks for the descriptors and refuses a shorter
	// file (archive_read_format_iso9660_bid); real volumes are far larger.
	const size_t total = std::max<size_t>(next, 24);
	std::string iso(total * Sector, '\0');
	const auto both32 = [&](const size_t at, const uint32_t v) {
		for (int i = 0; i < 4; ++i) {
			iso[at + i]     = static_cast<char>(v >> (8 * i));
			iso[at + 7 - i] = static_cast<char>(v >> (8 * i));
		}
	};
	// A directory record (ECMA-119 9.1): 33 bytes plus the identifier,
	// padded to an even length; the date is 2000-01-01, offset 0 (9.1.5).
	const auto record = [&](const size_t at,
	                        const std::string& ident,
	                        const uint32_t extent,
	                        const uint32_t length,
	                        const bool is_dir) {
		const size_t len = 33 + ident.size() +
		                   (ident.size() % 2 == 0 ? 1 : 0);
		iso[at] = static_cast<char>(len);
		both32(at + 2, extent);
		both32(at + 10, length);
		iso[at + 18] = 100;
		iso[at + 19] = 1;
		iso[at + 20] = 1;
		iso[at + 25] = is_dir ? 2 : 0;
		iso[at + 28] = 1;
		iso[at + 31] = 1;
		iso[at + 32] = static_cast<char>(ident.size());
		iso.replace(at + 33, ident.size(), ident);
		return len;
	};
	const auto upper = [](std::string s) {
		for (auto& c : s) {
			c = static_cast<char>(
			        std::toupper(static_cast<unsigned char>(c)));
		}
		return s;
	};
	for (size_t d = 0; d < dirs.size(); ++d) {
		size_t at = dirs[d].extent * Sector;
		at += record(at,
		             std::string(1, '\0'),
		             static_cast<uint32_t>(dirs[d].extent),
		             Sector,
		             true);
		at += record(at,
		             std::string(1, '\1'),
		             static_cast<uint32_t>(dirs[0].extent),
		             Sector,
		             true);
		if (d == 0) {
			for (size_t s = 1; s < dirs.size(); ++s) {
				at += record(at,
				             upper(dirs[s].name),
				             static_cast<uint32_t>(dirs[s].extent),
				             Sector,
				             true);
			}
		}
		for (const auto i : dirs[d].files) {
			const auto& path = files[i].name;
			const auto slash = path.find('/');
			const auto name  = upper(slash == std::string::npos
                                                        ? path
                                                        : path.substr(slash + 1)) +
			                  ";1";
			at += record(at,
			             name,
			             static_cast<uint32_t>(file_extent[i]),
			             static_cast<uint32_t>(files[i].data.size()),
			             false);
		}
	}
	for (size_t i = 0; i < files.size(); ++i) {
		iso.replace(file_extent[i] * Sector,
		            files[i].data.size(),
		            files[i].data);
	}
	// Path table records (ECMA-119 9.4): the root, then each directory
	// with the root as its parent; L little-endian, M big-endian.
	size_t table_size = 0;
	for (const bool big : {false, true}) {
		size_t at = (big ? PathTableM : PathTableL) * Sector;
		for (size_t d = 0; d < dirs.size(); ++d) {
			const auto ident = d == 0 ? std::string(1, '\0')
			                          : upper(dirs[d].name);
			iso[at]          = static_cast<char>(ident.size());
			const auto extent = static_cast<uint32_t>(dirs[d].extent);
			for (int i = 0; i < 4; ++i) {
				iso[at + 2 + (big ? 3 - i : i)] = static_cast<char>(
				        extent >> (8 * i));
			}
			iso[at + 6 + (big ? 1 : 0)] = 1; // the parent is the root
			iso.replace(at + 8, ident.size(), ident);
			at += 8 + ident.size() + (ident.size() % 2);
		}
		table_size = at - (big ? PathTableM : PathTableL) * Sector;
	}
	const size_t pvd = 16 * Sector;
	iso[pvd]         = 1;
	iso.replace(pvd + 1, 5, "CD001");
	iso[pvd + 6] = 1;
	iso.replace(pvd + 40, 32, "TEST                            ");
	both32(pvd + 80, static_cast<uint32_t>(total));
	iso[pvd + 120] = 1;
	iso[pvd + 123] = 1;
	iso[pvd + 124] = 1;
	iso[pvd + 127] = 1;
	iso[pvd + 129] = 8;
	iso[pvd + 130] = 8;
	both32(pvd + 132, static_cast<uint32_t>(table_size));
	for (int i = 0; i < 4; ++i) {
		iso[pvd + 140 + i] = static_cast<char>(PathTableL >> (8 * i));
		iso[pvd + 148 + 3 - i] = static_cast<char>(PathTableM >> (8 * i));
	}
	record(pvd + 156,
	       std::string(1, '\0'),
	       static_cast<uint32_t>(dirs[0].extent),
	       Sector,
	       true);
	iso[pvd + 881]          = 1; // file structure version (ECMA-119 8.4.31)
	const size_t terminator = 17 * Sector;
	iso[terminator]         = static_cast<char>(255);
	iso.replace(terminator + 1, 5, "CD001");
	iso[terminator + 6] = 1;
	return iso;
}

// One file at the root of a volume with a Joliet tree beside the ISO 9660
// one (Joliet spec, UCS-2 level 3 "%/E"); the Joliet name is written as
// given, ASCII only, up to the 110 characters a directory record holds.
inline std::string IsoWithJolietFile(const std::string& joliet_name,
                                     const std::string& data)
{
	constexpr size_t Sector = 2048;
	// 16 PVD, 17 SVD, 18 terminator, 19-22 the two trees' L and M path
	// tables, 23 and 24 the two root directories, then the file.
	constexpr size_t Pvd = 16, Svd = 17, Terminator = 18;
	constexpr size_t RootIso = 23, RootJoliet = 24, FileExtent = 25;
	const size_t total = std::max<size_t>(FileExtent + (data.size() + Sector -
	                                                    1) / Sector,
	                                      26);
	std::string iso(total * Sector, '\0');
	const auto both32 = [&](const size_t at, const uint32_t v) {
		for (int i = 0; i < 4; ++i) {
			iso[at + i]     = static_cast<char>(v >> (8 * i));
			iso[at + 7 - i] = static_cast<char>(v >> (8 * i));
		}
	};
	const auto record = [&](const size_t at,
	                        const std::string& ident,
	                        const uint32_t extent,
	                        const uint32_t length,
	                        const bool is_dir) {
		const size_t len = 33 + ident.size() +
		                   (ident.size() % 2 == 0 ? 1 : 0);
		assert(len <= 255);
		iso[at] = static_cast<char>(len);
		both32(at + 2, extent);
		both32(at + 10, length);
		iso[at + 18] = 100;
		iso[at + 19] = 1;
		iso[at + 20] = 1;
		iso[at + 25] = is_dir ? 2 : 0;
		iso[at + 28] = 1;
		iso[at + 31] = 1;
		iso[at + 32] = static_cast<char>(ident.size());
		iso.replace(at + 33, ident.size(), ident);
		return len;
	};
	auto ucs2 = std::string();
	for (const char c : joliet_name) {
		ucs2 += '\0';
		ucs2 += c;
	}
	const auto file_size = static_cast<uint32_t>(data.size());
	for (const auto& [root, name] : {
	             std::pair{   RootIso, std::string("LONG.ZIP;1")},
	             std::pair{RootJoliet,                      ucs2}
        }) {
		size_t at = root * Sector;
		at += record(at,
		             std::string(1, '\0'),
		             static_cast<uint32_t>(root),
		             Sector,
		             true);
		at += record(at,
		             std::string(1, '\1'),
		             static_cast<uint32_t>(root),
		             Sector,
		             true);
		record(at, name, static_cast<uint32_t>(FileExtent), file_size, false);
	}
	iso.replace(FileExtent * Sector, data.size(), data);
	// Each tree's path tables hold its root alone (ECMA-119 9.4).
	for (const auto& [table_l, root] : {
	             std::pair{size_t{19},    RootIso},
	             std::pair{size_t{21}, RootJoliet}
        }) {
		for (const bool big : {false, true}) {
			const size_t at = (table_l + (big ? 1 : 0)) * Sector;
			iso[at]         = 1;
			for (int i = 0; i < 4; ++i) {
				iso[at + 2 + (big ? 3 - i : i)] = static_cast<char>(
				        root >> (8 * i));
			}
			iso[at + 6 + (big ? 1 : 0)] = 1;
		}
	}
	for (const auto& [vd, table_l, root] : {
	             std::tuple{Pvd, size_t{19},    RootIso},
	             std::tuple{Svd, size_t{21}, RootJoliet}
        }) {
		const size_t at = vd * Sector;
		iso[at]         = vd == Pvd ? 1 : 2;
		iso.replace(at + 1, 5, "CD001");
		iso[at + 6] = 1;
		iso.replace(at + 40, 32, std::string(32, ' '));
		both32(at + 80, static_cast<uint32_t>(total));
		if (vd == Svd) {
			iso.replace(at + 88, 3, "%/E");
		}
		iso[at + 120] = 1;
		iso[at + 123] = 1;
		iso[at + 124] = 1;
		iso[at + 127] = 1;
		iso[at + 129] = 8;
		iso[at + 130] = 8;
		both32(at + 132, 10);
		for (int i = 0; i < 4; ++i) {
			iso[at + 140 + i] = static_cast<char>(table_l >> (8 * i));
			iso[at + 148 + 3 - i] = static_cast<char>((table_l + 1) >>
			                                          (8 * i));
		}
		record(at + 156,
		       std::string(1, '\0'),
		       static_cast<uint32_t>(root),
		       Sector,
		       true);
		iso[at + 881] = 1;
	}
	const size_t end = Terminator * Sector;
	iso[end]         = static_cast<char>(255);
	iso.replace(end + 1, 5, "CD001");
	iso[end + 6] = 1;
	return iso;
}

// Upstream libarchive cuts Joliet names at 103 characters (libarchive
// issue #3718); our vcpkg overlay patches that out, a system library does not.
inline bool LibarchiveReadsJolietName(const size_t length)
{
	assert(length > 4 && length <= 110);
	const auto name   = std::string(length - 4, 'j') + ".zip";
	const auto iso    = IsoWithJolietFile(name, "probe");
	struct archive* a = archive_read_new();
	if (a == nullptr) {
		return false;
	}
	archive_read_support_format_iso9660(a);
	bool whole = false;
	if (archive_read_open_memory(a, iso.data(), iso.size()) == ARCHIVE_OK) {
		struct archive_entry* entry = nullptr;
		while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
			const char* path = archive_entry_pathname_utf8(entry);
			whole = whole || (path != nullptr && name == path);
		}
	}
	archive_read_free(a);
	return whole;
}

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
		        (e.utf8_flag ? 0x800 : 0) |
		        (e.encrypted_flag ? 0x1 : 0) | e.extra_flags);
		put32(out, 0x04034b50u);
		put16(out, 20);
		put16(out, e.local_flags.value_or(flags));
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
		        (e.utf8_flag ? 0x800 : 0) |
		        (e.encrypted_flag ? 0x1 : 0) | e.extra_flags);
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

enum class Kind {
	ZipStored,
	ZipDeflate,
	SevenZip,
	Tar,
	TarGz,
	TarXz,
	TarZst,
	TarLzma
};

// libarchive's 7z and ISO writers convert names to UTF-16 from the
// locale's charset; the test process runs in "C", which has no
// characters outside ASCII.
class ScopedUtf8Ctype {
public:
	ScopedUtf8Ctype()
	{
		const char* current = std::setlocale(LC_CTYPE, nullptr);
		saved               = current ? current : "C";
		for (const char* name :
		     {"C.UTF-8", "C.utf8", "UTF-8", ".UTF-8", "en_US.UTF-8"}) {
			if (std::setlocale(LC_CTYPE, name)) {
				break;
			}
		}
	}
	~ScopedUtf8Ctype()
	{
		std::setlocale(LC_CTYPE, saved.c_str());
	}
	ScopedUtf8Ctype(const ScopedUtf8Ctype&)            = delete;
	ScopedUtf8Ctype& operator=(const ScopedUtf8Ctype&) = delete;

private:
	std::string saved = {};
};

// Writes an archive with libarchive's writer. Returns false when the
// writer refuses something (the test then fails with the reason).
inline bool WriteArchive(const std::filesystem::path& path, const Kind kind,
                         const std::vector<FileEntry>& entries,
                         const char* passphrase = nullptr,
                         std::string* error     = nullptr)
{
	const ScopedUtf8Ctype utf8_names = {};
	struct archive* a                = archive_write_new();
	int r                            = ARCHIVE_OK;
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
	case Kind::TarLzma:
		r = archive_write_set_format_pax_restricted(a);
		if (r == ARCHIVE_OK) {
			r = archive_write_add_filter_lzma(a);
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
		r = OpenWriterAt(a, path);
	}
	for (const auto& e : entries) {
		if (r != ARCHIVE_OK) {
			break;
		}
		struct archive_entry* ae = archive_entry_new();
		archive_entry_set_pathname_utf8(ae, e.name.c_str());
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

enum class Filter { Gzip, Bzip2, Xz, Zstd, Lzma };

// One compressed stream of bytes through libarchive's raw writer.
inline bool WriteCompressed(const std::filesystem::path& path, const Filter filter,
                            const std::string& bytes, std::string* error = nullptr)
{
	struct archive* a = archive_write_new();
	int r             = archive_write_set_format_raw(a);
	if (r == ARCHIVE_OK) {
		switch (filter) {
		case Filter::Gzip: r = archive_write_add_filter_gzip(a); break;
		case Filter::Bzip2: r = archive_write_add_filter_bzip2(a); break;
		case Filter::Xz: r = archive_write_add_filter_xz(a); break;
		case Filter::Zstd: r = archive_write_add_filter_zstd(a); break;
		case Filter::Lzma: r = archive_write_add_filter_lzma(a); break;
		}
	}
	if (r == ARCHIVE_OK) {
		r = OpenWriterAt(a, path);
	}
	if (r == ARCHIVE_OK) {
		struct archive_entry* ae = archive_entry_new();
		archive_entry_set_pathname(ae, "data");
		archive_entry_set_filetype(ae, AE_IFREG);
		archive_entry_set_size(ae, static_cast<la_int64_t>(bytes.size()));
		r = archive_write_header(a, ae);
		if (r == ARCHIVE_OK &&
		    archive_write_data(a, bytes.data(), bytes.size()) !=
		            static_cast<la_ssize_t>(bytes.size())) {
			r = ARCHIVE_FATAL;
		}
		archive_entry_free(ae);
	}
	if (r == ARCHIVE_OK) {
		r = archive_write_close(a);
	}
	if (r != ARCHIVE_OK && error) {
		*error = archive_error_string(a) ? archive_error_string(a) : "write failed";
	}
	archive_write_free(a);
	return r == ARCHIVE_OK;
}

enum class IszChunk : uint8_t { Zeros = 0, Stored = 1, Zlib = 2, Bzip2 = 3 };

// Header field offsets, EZB ISZ spec v1.00 section 3.1 (packed, little endian).
namespace IszField {
constexpr size_t HeaderSize   = 4;
constexpr size_t SectSize     = 10;
constexpr size_t TotalSectors = 12;
constexpr size_t HasPassword  = 16;
constexpr size_t SegmentSize  = 17;
constexpr size_t NBlocks      = 25;
constexpr size_t BlockSize    = 29;
constexpr size_t PtrLen       = 33;
constexpr size_t PtrOffs      = 35;
constexpr size_t SegOffs      = 39;
constexpr size_t DataOffs     = 43;
} // namespace IszField

struct IszOptions {
	uint32_t block_size = 64 * 1024;
	uint8_t ptr_len     = 3;    // what UltraISO writes
	bool table          = true; // false: ptr_offs 0, the image raw at data_offs
	uint8_t password    = 0;
	uint32_t seg_offs   = 0;
	bool mangle_bzip2   = true; // UltraISO stores "IsZ" over "BZh"
	bool trailer        = true; // UltraISO's 68 bytes after the last chunk
};

inline void PatchLe(std::string& bytes, const size_t at, const uint64_t value,
                    const size_t width)
{
	for (size_t i = 0; i < width; ++i) {
		bytes[at + i] = static_cast<char>((value >> (8 * i)) & 0xff);
	}
}

// An ISZ of image (a multiple of 2048 bytes), every chunk of one kind; an
// all-zero chunk becomes Zeros under Zlib and Bzip2, as UltraISO does.
// Empty when a chunk length does not fit ptr_len or a codec fails.
inline std::string MakeIsz(const std::string& image, const IszChunk kind,
                           const IszOptions& o = {})
{
	const auto block   = static_cast<size_t>(o.block_size);
	const auto nchunks = (image.size() + block - 1) / block;
	const auto bits    = o.ptr_len * 8;
	std::string table  = {};
	std::string data   = {};
	if (o.table) {
		for (size_t i = 0; i < nchunks; ++i) {
			const auto chunk = image.substr(i * block, block);
			auto type        = kind;
			const bool zero  = chunk.find_first_not_of('\0') == std::string::npos;
			if (zero && (kind == IszChunk::Zlib || kind == IszChunk::Bzip2 ||
			             kind == IszChunk::Zeros)) {
				type = IszChunk::Zeros;
			} else if (kind == IszChunk::Zeros) {
				type = IszChunk::Stored;
			}
			std::string stored = {};
			if (type == IszChunk::Stored) {
				stored = chunk;
			} else if (type == IszChunk::Zlib) {
				auto out_len = compressBound(static_cast<uLong>(chunk.size()));
				stored.resize(out_len);
				if (compress2(reinterpret_cast<Bytef*>(stored.data()),
				              &out_len,
				              reinterpret_cast<const Bytef*>(chunk.data()),
				              static_cast<uLong>(chunk.size()),
				              9) != Z_OK) {
					return {};
				}
				stored.resize(out_len);
			} else if (type == IszChunk::Bzip2) {
				auto out_len = static_cast<unsigned>(chunk.size() + chunk.size() / 100 + 600);
				stored.resize(out_len);
				auto in = chunk;
				if (BZ2_bzBuffToBuffCompress(stored.data(), &out_len, in.data(),
				                             static_cast<unsigned>(in.size()), 9, 0, 0) != BZ_OK) {
					return {};
				}
				stored.resize(out_len);
				if (o.mangle_bzip2) {
					stored.replace(0, 3, "IsZ");
				}
			}
			const auto len = static_cast<uint64_t>(stored.size());
			if (bits < 64 && len >= (uint64_t{1} << (bits - 2))) {
				return {};
			}
			const auto value = (static_cast<uint64_t>(type) << (bits - 2)) | len;
			for (size_t b = 0; b < o.ptr_len; ++b) {
				table.push_back(static_cast<char>((value >> (8 * b)) & 0xff));
			}
			data += stored;
		}
		for (size_t i = 0; i < table.size(); ++i) {
			static constexpr uint8_t key[4] = {0xB6, 0x8C, 0xA5, 0xDE};
			table[i] = static_cast<char>(static_cast<uint8_t>(table[i]) ^ key[i % 4]);
		}
	} else {
		data = image;
	}
	const size_t ptr_offs  = o.table ? 48 : 0;
	const size_t table_end = 48 + table.size();
	const size_t data_offs = (table_end + 3) / 4 * 4; // UltraISO rounds to four
	std::string out(data_offs, '\0');
	out.replace(0, 4, "IsZ!");
	PatchLe(out, IszField::HeaderSize, 48, 1);
	PatchLe(out, 5, 1, 1);
	PatchLe(out, 6, 0x12345678, 4);
	PatchLe(out, IszField::SectSize, 2048, 2);
	PatchLe(out, IszField::TotalSectors, image.size() / 2048, 4);
	PatchLe(out, IszField::HasPassword, o.password, 1);
	PatchLe(out, IszField::NBlocks, nchunks, 4);
	PatchLe(out, IszField::BlockSize, o.block_size, 4);
	PatchLe(out, IszField::PtrLen, o.ptr_len, 1);
	PatchLe(out, IszField::PtrOffs, ptr_offs, 4);
	PatchLe(out, IszField::SegOffs, o.seg_offs, 4);
	PatchLe(out, IszField::DataOffs, data_offs, 4);
	out.replace(48, table.size(), table);
	out += data;
	if (o.trailer) {
		auto trailer = out.substr(0, 48) + std::string(16, '\0');
		trailer.resize(68, '\0');
		PatchLe(trailer, 64, 68, 4);
		out += trailer;
	}
	return out;
}

inline bool WriteIsz(const std::filesystem::path& path, const std::string& image,
                     const IszChunk kind, const IszOptions& o = {})
{
	const auto bytes = MakeIsz(image, kind, o);
	if (bytes.empty()) {
		return false;
	}
	WriteBytes(path, std::vector<uint8_t>(bytes.begin(), bytes.end()));
	return std::filesystem::file_size(path) == bytes.size();
}

} // namespace ArchiveFixtures

#endif // DOSBOX_TESTS_ARCHIVE_FIXTURES_H
