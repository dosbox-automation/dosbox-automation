// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_ARCHIVE_CHOOSER_H
#define DOSBOX_ARCHIVE_CHOOSER_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ArchiveChooser {

enum class Category { Game, Setup, Other, Conf };

struct Entry {
	Category category = Category::Other;
	std::string exe   = {};
	std::string args  = {};
	std::string label = {};
};

struct Menu {
	std::string title          = {};
	std::string drive          = {};
	std::vector<Entry> entries = {};
	// The entry CHOOSER.TML marks to run without showing the menu.
	std::optional<size_t> autostart = {};
};

struct ProgramScan {
	std::vector<Entry> entries = {};
	bool has_dosbox_bat        = false;
};

// Programs at the root and one folder level, in menu order and capped.
ProgramScan ScanPrograms(char drive_letter);

Menu ProgramMenu(char drive_letter, std::vector<Entry> entries);

// Parse CHOOSER.TML content into a menu; the file's own title wins over
// `title`. Returns nullopt on a parse error or when no entry is valid.
std::optional<Menu> ParseChooserToml(std::string_view toml_text,
                                     std::string_view title);

// CHOOSER.TML at the root of a mounted drive, read through the drive.
// Nothing when it is missing, larger than 16 KiB or has no valid entry.
std::optional<Menu> ReadChooser(char drive_letter);

// Serialize a Menu into the text format UniMenu reads through the
// port: title, drive, blank line, then [CATEGORY]\nexe args\nlabel
// groups.
std::string SerializeMenu(const Menu& menu);

// Strip control characters (< 0x20 and 0x7F), truncate to 60 chars.
std::string SanitizeLabel(std::string_view raw);

// Control characters and the characters a DOS command line reads as syntax.
bool HasShellCharacter(std::string_view text);

// True when Resolve would accept the path as an 8.3 name, its extension, if
// any, is .COM, .EXE or .BAT, and it holds no `..` or shell character.
bool IsValidExePath(std::string_view path);

// The args rule of CHOOSER.TML: at most 40 characters, no control
// characters and none of > < | & % ;.
bool IsValidArgs(std::string_view args);

} // namespace ArchiveChooser

#endif
