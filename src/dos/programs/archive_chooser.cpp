// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/programs/archive_chooser.h"

#include "dos/archive/archive_toml.h"
#include "dos/dos.h"
#include "dos/drives.h"
#include "dos/programs/append.h"
#include "dos/programs/mount_run.h"
#include "misc/support.h"
#include "utils/checks.h"
#include "utils/string_utils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

CHECK_NARROWING();

namespace ArchiveChooser {

namespace {

using MountRun::AsciiUpper;
using MountRun::TempDtaSearch;

constexpr size_t MaxEntries        = 50;
constexpr size_t MaxChooserEntries = 20;
constexpr size_t MaxLabelLength    = 60;
constexpr size_t MaxLaunchers      = 500;
constexpr size_t MaxExePathLen     = 64;
constexpr size_t MaxArgsLength     = 40;
constexpr size_t MaxChooserSize    = 16 * 1024;

struct KnownExe {
	std::string name  = {};
	Category category = Category::Other;
};

// '*' matches any run of characters, every other character itself.
bool MatchGlob(const std::string_view name, const std::string_view pattern)
{
	size_t n = 0;
	size_t p = 0;
	// Where the last star sits and how far the name had got then, for
	// backtracking when a literal part fails further on.
	auto star       = std::string_view::npos;
	size_t resume_n = 0;
	while (n < name.size()) {
		if (p < pattern.size() && pattern[p] == '*') {
			star     = p++;
			resume_n = n;
		} else if (p < pattern.size() && pattern[p] == name[n]) {
			++p;
			++n;
		} else if (star != std::string_view::npos) {
			p = star + 1;
			n = ++resume_n;
		} else {
			return false;
		}
	}
	while (p < pattern.size() && pattern[p] == '*') {
		++p;
	}
	return p == pattern.size();
}

bool MatchesAnyPattern(const std::string_view upper_name,
                       const std::vector<std::string_view>& patterns)
{
	const auto dot  = upper_name.rfind('.');
	const auto base = (dot == std::string_view::npos)
	                        ? upper_name
	                        : upper_name.substr(0, dot);
	for (const auto pattern : patterns) {
		const auto pdot = pattern.find('.');
		if (pdot == std::string_view::npos) {
			if (MatchGlob(base, pattern)) {
				return true;
			}
		} else {
			const auto pbase = pattern.substr(0, pdot);
			const auto pext  = pattern.substr(pdot + 1);
			const auto ext   = (dot == std::string_view::npos)
			                         ? std::string_view{}
			                         : upper_name.substr(dot + 1);
			if (MatchGlob(base, pbase) && MatchGlob(ext, pext)) {
				return true;
			}
		}
	}
	return false;
}

constexpr std::array<std::string_view, 7> GamePatterns = {
        "RUN.*",
        "RUNME.*",
        "LAUNCH*.*",
        "START*.*",
        "GAME.*",
        "PLAY.*",
        "GO.*",
};

constexpr std::array<std::string_view, 8> SetupPatterns = {
        "INSTALL*.*",
        "SETUP*.*",
        "CONFIG*.*",
        "*CFG*.*",
        "*SOUND*.*",
        "*DIAG*.*",
        "*PATCH*.*",
        "SETM*.*",
};

std::vector<std::string_view> GamePatternVec()
{
	return {GamePatterns.begin(), GamePatterns.end()};
}

std::vector<std::string_view> SetupPatternVec()
{
	return {SetupPatterns.begin(), SetupPatterns.end()};
}

std::vector<KnownExe> ReadLaunchers()
{
	std::vector<KnownExe> known = {};
	const auto path = get_resource_path("drives/y/dos", "launchers.txt");
	if (path.empty()) {
		return known;
	}
	std::ifstream file(path);
	if (!file.is_open()) {
		return known;
	}
	std::string line = {};
	while (std::getline(file, line) && known.size() < MaxLaunchers) {
		if (line.empty() || line[0] == '#') {
			continue;
		}
		if (line.size() < 3 || line[1] != ' ') {
			continue;
		}
		const char tag = static_cast<char>(
		        std::toupper(static_cast<unsigned char>(line[0])));
		if (tag != 'G' && tag != 'S') {
			continue;
		}
		KnownExe entry = {};
		entry.name     = AsciiUpper(line.substr(2));
		entry.category = (tag == 'G') ? Category::Game : Category::Setup;
		known.push_back(std::move(entry));
	}
	return known;
}

Category ClassifyExe(const std::string_view upper_name,
                     const std::vector<KnownExe>& known)
{
	// Exact match in LAUNCHERS.TXT wins.
	for (const auto& k : known) {
		if (k.name == upper_name) {
			return k.category;
		}
	}
	if (MatchesAnyPattern(upper_name, GamePatternVec())) {
		return Category::Game;
	}
	if (MatchesAnyPattern(upper_name, SetupPatternVec())) {
		return Category::Setup;
	}
	return Category::Other;
}

bool IsProgramExtension(const std::string_view ext)
{
	return ext == "COM" || ext == "EXE" || ext == "BAT";
}

bool ShouldSkip(const std::string_view upper_name)
{
	return upper_name.starts_with("_") || upper_name == "DOSBOX.BAT" ||
	       upper_name == "AUTOEXEC.BAT";
}

std::string LabelFromExeName(const std::string_view name)
{
	const auto dot = name.rfind('.');
	if (dot == std::string_view::npos) {
		return std::string(name);
	}
	return std::string(name.substr(0, dot));
}

const char* CategoryTag(const Category cat)
{
	switch (cat) {
	case Category::Game: return "GAME";
	case Category::Setup: return "SETUP";
	case Category::Other: return "OTHER";
	case Category::Conf: return "CONF";
	}
	return "OTHER";
}

Category CategoryFromString(const std::string_view text)
{
	const auto upper = AsciiUpper(text);
	if (upper == "GAME") {
		return Category::Game;
	}
	if (upper == "SETUP") {
		return Category::Setup;
	}
	if (upper == "CONF") {
		return Category::Conf;
	}
	return Category::Other;
}

} // namespace

std::string SanitizeLabel(const std::string_view raw)
{
	std::string clean = {};
	for (const char c : raw) {
		const auto byte = static_cast<uint8_t>(c);
		if (byte >= 0x20 && byte != 0x7f) {
			clean += c;
		}
		if (clean.size() >= MaxLabelLength) {
			break;
		}
	}
	return clean;
}

bool IsValidArgs(const std::string_view args)
{
	if (args.size() > MaxArgsLength) {
		return false;
	}
	return !HasShellCharacter(args);
}

bool HasShellCharacter(const std::string_view text)
{
	return std::any_of(text.begin(), text.end(), [](const char c) {
		const auto byte = static_cast<uint8_t>(c);
		return byte < 0x20 || byte == 0x7f || c == '>' || c == '<' ||
		       c == '|' || c == '&' || c == '%' || c == ';';
	});
}

bool IsValidExePath(const std::string_view path)
{
	if (path.size() > MaxExePathLen || HasShellCharacter(path) ||
	    path.find("..") != std::string_view::npos ||
	    !MountRun::IsProgramName(path)) {
		return false;
	}
	const auto name = path.substr(path.find_last_of("\\/") + 1);
	const auto dot  = name.find('.');
	return dot == std::string_view::npos ||
	       IsProgramExtension(AsciiUpper(name.substr(dot + 1)));
}

ProgramScan ScanPrograms(const char drive_letter)
{
	const auto letter = static_cast<char>(
	        std::toupper(static_cast<unsigned char>(drive_letter)));
	if (letter < 'A' || letter > 'Z') {
		return {};
	}
	const auto index = drive_index(letter);
	if (!Drives.at(index)) {
		return {};
	}

	auto known = ReadLaunchers();

	AppendCommand::Suspend no_append;
	TempDtaSearch search = {};

	std::vector<Entry> entries = {};
	bool has_dosbox_bat        = false;

	const auto root_pattern = std::string(1, letter) + ":\\*.*";
	auto entry              = search.First(root_pattern);
	while (entry) {
		if (entry->IsFile()) {
			const auto upper = AsciiUpper(entry->name);
			if (upper == "DOSBOX.BAT") {
				has_dosbox_bat = true;
			}
			const auto ext = entry->GetExtension();
			if (IsProgramExtension(ext) && !ShouldSkip(upper) &&
			    IsValidExePath(upper)) {
				Entry e    = {};
				e.exe      = upper;
				e.label    = LabelFromExeName(upper);
				e.category = ClassifyExe(upper, known);
				entries.push_back(std::move(e));
			}
		}
		entry = search.Next();
	}

	std::vector<std::string> subdirs = {};
	{
		TempDtaSearch dir_search = {};
		auto dir_entry           = dir_search.First(root_pattern);
		while (dir_entry) {
			if (dir_entry->IsDirectory() &&
			    !dir_entry->IsDummyDirectory()) {
				subdirs.push_back(AsciiUpper(dir_entry->name));
			}
			dir_entry = dir_search.Next();
		}
	}
	for (const auto& dir_name : subdirs) {
		const auto sub_pattern = std::string(1, letter) + ":\\" +
		                         dir_name + "\\*.*";
		TempDtaSearch sub_search = {};
		auto sub_entry           = sub_search.First(sub_pattern);
		while (sub_entry) {
			if (sub_entry->IsFile()) {
				const auto upper = AsciiUpper(sub_entry->name);
				const auto ext   = sub_entry->GetExtension();
				const auto path  = dir_name + "\\" + upper;
				if (IsProgramExtension(ext) &&
				    !ShouldSkip(upper) && IsValidExePath(path)) {
					Entry e = {};
					e.exe   = path;
					e.label = dir_name + "\\" +
					          LabelFromExeName(upper);
					e.category = ClassifyExe(upper, known);
					entries.push_back(std::move(e));
				}
			}
			sub_entry = sub_search.Next();
		}
	}

	// An archive drive lists in member order, so the menu sorts itself; the
	// cap comes after the sort and cuts from the end.
	const auto in_folder = [](const Entry& e) {
		return e.exe.find('\\') != std::string::npos;
	};
	std::sort(entries.begin(),
	          entries.end(),
	          [&in_folder](const Entry& a, const Entry& b) {
		          return std::tuple(static_cast<int>(a.category),
		                            in_folder(a),
		                            std::string_view(a.exe)) <
		                 std::tuple(static_cast<int>(b.category),
		                            in_folder(b),
		                            std::string_view(b.exe));
	          });
	if (entries.size() > MaxEntries) {
		entries.resize(MaxEntries);
	}
	return {std::move(entries), has_dosbox_bat};
}

Menu ProgramMenu(const char drive_letter, std::vector<Entry> entries)
{
	Menu menu    = {};
	menu.title   = "Select a program";
	menu.drive   = std::string(1,
                                 static_cast<char>(std::toupper(
                                         static_cast<unsigned char>(drive_letter))));
	menu.entries = std::move(entries);
	return menu;
}

std::optional<Menu> ParseChooserToml(const std::string_view toml_text,
                                     const std::string_view title)
{
	std::string error = {};
	auto doc          = ArchiveToml::Parse(toml_text, &error);
	if (!doc) {
		return {};
	}

	const auto tables = doc->Array("entry");
	if (tables.empty()) {
		return {};
	}

	Menu menu             = {};
	const auto file_title = doc->root.String("title");
	menu.title = file_title ? SanitizeLabel(*file_title) : std::string(title);

	for (const auto* table : tables) {
		if (menu.entries.size() >= MaxChooserEntries) {
			break;
		}
		const auto label = table->String("label");
		const auto exe   = table->String("exe");
		if (!label || !exe) {
			continue;
		}
		if (!IsValidExePath(*exe)) {
			continue;
		}
		const auto args = table->String("args");
		if (args && !IsValidArgs(*args)) {
			continue;
		}
		const auto autostart = table->Boolean("autostart");
		if (autostart && *autostart && !menu.autostart) {
			menu.autostart = menu.entries.size();
		}

		Entry entry    = {};
		entry.label    = SanitizeLabel(*label);
		entry.exe      = AsciiUpper(*exe);
		entry.category = Category::Game;

		const auto type = table->String("type");
		if (type) {
			entry.category = CategoryFromString(*type);
		}

		if (args) {
			entry.args = *args;
		}

		menu.entries.push_back(std::move(entry));
	}

	if (menu.entries.empty()) {
		return {};
	}
	return menu;
}

std::optional<Menu> ReadChooser(const char drive_letter)
{
	const auto letter = static_cast<char>(
	        std::toupper(static_cast<unsigned char>(drive_letter)));
	if (letter < 'A' || letter > 'Z') {
		return {};
	}
	const auto& drive = Drives.at(drive_index(letter));
	if (!drive) {
		return {};
	}
	auto file = drive->FileOpen("CHOOSER.TML", OPEN_READ);
	if (!file) {
		return {};
	}
	std::string text                 = {};
	std::array<uint8_t, 4096> buffer = {};
	bool ok                          = true;
	for (;;) {
		auto size = static_cast<uint16_t>(buffer.size());
		if (!file->Read(buffer.data(), &size)) {
			ok = false;
			break;
		}
		if (size == 0) {
			break;
		}
		text.append(reinterpret_cast<const char*>(buffer.data()), size);
		if (text.size() > MaxChooserSize) {
			ok = false;
			break;
		}
	}
	file->Close();
	if (!ok) {
		return {};
	}
	auto menu = ParseChooserToml(text, "Select a program");
	if (menu) {
		menu->drive = std::string(1, letter);
	}
	return menu;
}

std::string SerializeMenu(const Menu& menu)
{
	std::string text = {};
	text += menu.title + "\n";
	text += menu.drive + "\n";
	text += "\n";

	for (const auto& entry : menu.entries) {
		text += "[";
		text += CategoryTag(entry.category);
		text += "]\n";
		text += entry.exe;
		if (!entry.args.empty()) {
			text += " " + entry.args;
		}
		text += "\n";
		text += entry.label + "\n";
	}
	return text;
}

} // namespace ArchiveChooser
