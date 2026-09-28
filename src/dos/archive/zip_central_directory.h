// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#ifndef DOSBOX_ARCHIVE_ZIP_CENTRAL_DIRECTORY_H
#define DOSBOX_ARCHIVE_ZIP_CENTRAL_DIRECTORY_H

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace ZipCentralDirectory {

struct Record {
	std::string raw_name         = {};    // bytes as stored, no conversion
	bool utf8_flag               = false; // general purpose bit 11
	bool encrypted               = false; // bit 0 or bit 6
	uint16_t method              = 0;
	uint32_t crc32               = 0;
	uint32_t dos_datetime        = 0; // time low, date high, as stored
	uint64_t compressed_size     = 0;
	uint64_t uncompressed_size   = 0;
	uint64_t local_header_offset = 0;
	uint32_t external_attributes = 0;
};

enum class Error {
	None,
	NotAZip,             // no end record in the last 64 KiB + 22 bytes
	Truncated,           // a record runs past the end of the data
	Malformed,           // a signature or a size does not add up
	MultiDisk,           // disk numbers other than 0 (spanned archive)
	TooManyEntries,      // the end record's count exceeds the cap
	IoFailed,            // the file could not be read (descriptors, disk)
	LocalHeaderMismatch, // a local header disagrees with its record
	DuplicateOffset,     // two records name one local header
};

const char* ErrorText(Error error);

struct Result {
	std::vector<Record> records = {};
	Error error                 = Error::None;
};

// The central directory bytes already in memory, with the entry count
// from the end record. Pure; the tests feed it hand-built bytes.
Result Parse(std::span<const uint8_t> central_directory, uint64_t entry_count,
             uint64_t max_entries);

// Reads the end record, the zip64 records when present, and the central
// directory from a file. Reads at most 64 MiB of directory.
Result Read(const std::filesystem::path& path, uint64_t max_entries);

// The same over an open descriptor, which the caller keeps: the read
// works on a duplicate, so the caller's position is untouched.
Result Read(int fd, uint64_t max_entries);

// The entry name as UTF-8: as stored when the UTF-8 flag is set, else
// decoded from code page 437, the zip default (APPNOTE appendix D).
std::string DecodeName(const Record& record);

// Which optional codecs this libarchive decodes itself.
struct Decoders {
	bool bzip2 = true;
	bool xz    = true; // also lzma
	bool zstd  = true;
};

// APPNOTE 4.4.5: stored and deflate always; bzip2 (12), lzma (14), xz
// (95) and zstd (93) with their decoder; ppmd (98) always; PKZIP 1.x's
// shrink, reduce and implode never (DOS-era zips carry them).
bool MethodReadable(uint16_t method, const Decoders& decoders);

constexpr uint64_t MaxDirectoryBytes = 64ull * 1024 * 1024;

} // namespace ZipCentralDirectory

#endif // DOSBOX_ARCHIVE_ZIP_CENTRAL_DIRECTORY_H
