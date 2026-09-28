// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#ifndef DOSBOX_ARCHIVE_ENTRY_NAMES_H
#define DOSBOX_ARCHIVE_ENTRY_NAMES_H

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ArchiveNames {

enum class Rejection {
	None,
	AbsolutePath,
	DotComponent,
	EmptyComponent,
	ControlCharacter,
	ComponentTooLong,
	ReservedDeviceName,
};

const char* RejectionText(Rejection rejection);

struct SplitPath {
	std::vector<std::string> components = {};
	bool is_dir                         = false;
};

constexpr int MaxComponentBytes         = 255;
constexpr unsigned int MaxNumberedNames = 999;

// Splits an entry path on '/' and '\', trims trailing dots and spaces
// from every component (Win32 does the same before resolution), and
// applies the name rules of the archive mount design, section 1.4.
std::optional<SplitPath> SplitEntryPath(std::string_view utf8_path,
                                        Rejection* rejection);

// One component to an 8.3 name: every non-ASCII code point becomes '_',
// sfn_clean_basis upcases and replaces the FAT-illegal characters, and a
// name that does not fit, or a number above 1, gets the ~number tail.
std::string FoldComponent(std::string_view utf8_component, unsigned int number);

// Unique 8.3 names per directory. DOS paths are uppercase with
// backslashes; the root's children have no leading backslash.
class DosNameTable {
public:
	// The DOS path of the new entry, or nothing when the 999 numbered
	// names are taken or the path would not fit DOS_MakeName (78 characters).
	std::optional<std::string> Assign(const std::string& parent_dos_path,
	                                  std::string_view utf8_component);
	bool Contains(const std::string& dos_path) const;

private:
	std::unordered_map<std::string, std::unordered_set<std::string>> names_per_dir = {};
};

} // namespace ArchiveNames

#endif // DOSBOX_ARCHIVE_ENTRY_NAMES_H
