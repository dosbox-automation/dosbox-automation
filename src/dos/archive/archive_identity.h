// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#ifndef DOSBOX_ARCHIVE_IDENTITY_H
#define DOSBOX_ARCHIVE_IDENTITY_H

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ArchiveIdentity {

struct ListingEntry {
	std::string utf8_path = {};
	int64_t size          = 0;
	int64_t mtime         = 0; // seconds since the epoch
	bool has_crc          = false;
	uint32_t crc32        = 0;
};

std::string Sha256Hex(std::span<const uint8_t> bytes);

// One line per entry sorted by path, "path<TAB>size<TAB>mtime<TAB>crc"
// with the CRC as eight hex digits or "-", then "length<TAB>bytes" for
// the archive file itself (design D2, with Edda's additions).
std::string CanonicalListing(std::vector<ListingEntry> entries,
                             int64_t archive_length);

// Lowercase hex, 64 digits.
std::string Hash(const std::vector<ListingEntry>& entries, int64_t archive_length);

// [A-Za-z0-9._-] kept, everything else '_'; "", "." and ".." become
// "archive" so a key is never empty or a dot name.
std::string SanitizeStem(std::string_view stem);

// "<sanitized stem>-<first 8 hex digits of the hash>"
std::string ShortKey(std::string_view stem, std::string_view hash_hex);

} // namespace ArchiveIdentity

#endif // DOSBOX_ARCHIVE_IDENTITY_H
