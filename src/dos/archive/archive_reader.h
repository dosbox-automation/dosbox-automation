// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#ifndef DOSBOX_ARCHIVE_READER_H
#define DOSBOX_ARCHIVE_READER_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ArchiveMount {

enum class Format { Zip, Rar, Rar5, SevenZip, Tar };

// Whether one member can be reached without inflating the ones before
// it (design D5): zip through the seekable reader, RAR v4 by header
// walking; 7z, RAR5 and every tar.* only in one sequential pass.
enum class Access { PerMember, Sequential };

enum class Error {
	None,
	OpenFailed,
	NotAnArchive,
	TooLarge, // declared total or one member over the cap
	TooManyEntries,
	Encrypted,
	MultiVolume,
	SolidUnsupported, // solid RAR v4: libarchive cannot read it at all
	EntryRejected,    // a name failed the rules; detail names the entry
	Corrupt,          // libarchive reported damage or a CRC mismatch
	Changed,          // the archive's size or mtime moved since the index
	Unsupported, // a member uses a compression method nobody here reads
	NotFound,    // no entry with that ordinal
};

const char* ErrorText(Error error);

struct Limits {
	int64_t max_entries      = 65536;
	int64_t max_total_bytes  = 4ll * 1024 * 1024 * 1024;
	int64_t max_member_bytes = 2ll * 1024 * 1024 * 1024;
};

struct Entry {
	int ordinal           = 0;  // header index in the archive, from 0
	std::string utf8_path = {}; // as normalized: separators '/', trimmed
	std::string dos_path  = {}; // uppercase 8.3 components, backslashes
	bool is_dir           = false;
	int64_t size          = 0;
	int64_t mtime         = 0;
	bool has_crc          = false;
	uint32_t crc32        = 0;
};

class ArchiveReader;

struct OpenResult {
	std::unique_ptr<ArchiveReader> reader = {};
	Error error                           = Error::None;
	std::string detail                    = {};
};

class ArchiveReader {
public:
	static OpenResult Open(const std::filesystem::path& path,
	                       const Limits& limits);
	~ArchiveReader();
	ArchiveReader(const ArchiveReader&)            = delete;
	ArchiveReader& operator=(const ArchiveReader&) = delete;

	Format GetFormat() const;
	Access GetAccess() const;
	const std::vector<Entry>& Entries() const;
	int64_t TotalBytes() const;
	int64_t ArchiveLength() const;
	const std::string& IdentityHash() const;

	// One call at a time: every handle shares the archive's file offset
	// (asserted in debug builds).
	// Contract: out holds exactly the declared size, or the error says why.
	Error Materialize(int ordinal, std::vector<uint8_t>& out, std::string* detail);
	// Contract: target appears whole or not at all (temp name, then rename)
	// and carries the entry's mtime.
	Error MaterializeToFile(int ordinal, const std::filesystem::path& target,
	                        std::string* detail);
	// Contract: one pass, each regular file at directory/<ordinal>; the
	// only way a sequential format is served.
	Error MaterializeAll(const std::filesystem::path& directory,
	                     std::string* detail);

private:
	// unique_ptr's deleter needs Impl complete, so the constructor lives
	// in the .cpp and the member carries no initializer (it is null).
	ArchiveReader();
	struct Impl;
	std::unique_ptr<Impl> impl;
};

} // namespace ArchiveMount

#endif // DOSBOX_ARCHIVE_READER_H
