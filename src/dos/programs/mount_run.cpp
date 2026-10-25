// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/programs/mount_run.h"

#include "dos/programs/append.h"

#include "dos/dos.h"
#include "dos/drives.h"
#include "utils/checks.h"
#include "utils/string_utils.h"

#include <algorithm>
#include <array>
#include <optional>

CHECK_NARROWING();

namespace MountRun {

std::string AsciiUpper(const std::string_view text)
{
	std::string upper(text);
	for (auto& c : upper) {
		if (c >= 'a' && c <= 'z') {
			c = static_cast<char>(c - 'a' + 'A');
		}
	}
	return upper;
}

TempDtaSearch::TempDtaSearch() : saved_dta(dos.dta())
{
	dos.dta(dos.tables.tempdta);
}

TempDtaSearch::~TempDtaSearch()
{
	dos.dta(saved_dta);
}

std::optional<DOS_DTA::Result> TempDtaSearch::First(const std::string& pattern)
{
	if (!DOS_FindFirst(pattern.c_str(), FatAttributeFlags::NotVolume)) {
		return {};
	}
	return Current();
}

std::optional<DOS_DTA::Result> TempDtaSearch::Next()
{
	if (!DOS_FindNext()) {
		return {};
	}
	return Current();
}

DOS_DTA::Result TempDtaSearch::Current()
{
	DOS_DTA::Result result = {};
	DOS_DTA(dos.dta()).GetResult(result);
	return result;
}

namespace {

constexpr size_t MaxNameLength = 64;

// The DOS search order for a name given without an extension.
constexpr std::array<std::string_view, 3> ProgramExtensions = {"COM", "EXE", "BAT"};

// The DOS filename set less anything a shell line could read as syntax.
bool IsNameCharacter(const char c)
{
	constexpr std::string_view Punctuation = "_~!#$%'()@^{}-";
	return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
	       Punctuation.find(c) != std::string_view::npos;
}

bool IsShortName(const std::string_view component)
{
	const auto dot  = component.find('.');
	const auto base = component.substr(0, dot);
	const auto ext  = (dot == std::string_view::npos)
	                        ? std::string_view{}
	                        : component.substr(dot + 1);
	// A leading '-' would make MOUNT read the name as an option.
	if (base.empty() || base.size() > 8 || ext.size() > 3 ||
	    (dot != std::string_view::npos && ext.empty()) || base.front() == '-') {
		return false;
	}
	return std::all_of(base.begin(), base.end(), IsNameCharacter) &&
	       std::all_of(ext.begin(), ext.end(), IsNameCharacter);
}

// The components of a name relative to the root, one leading separator
// allowed; empty when any part is not an 8.3 name.
std::vector<std::string> SplitName(const std::string_view name)
{
	if (name.empty() || name.size() > MaxNameLength) {
		return {};
	}
	auto rest = std::string_view(name);
	if (rest.front() == '\\' || rest.front() == '/') {
		rest.remove_prefix(1);
	}
	std::vector<std::string> components = {};
	while (true) {
		const auto end       = rest.find_first_of("\\/");
		const auto component = AsciiUpper(rest.substr(0, end));
		if (!IsShortName(component)) {
			return {};
		}
		components.push_back(component);
		if (end == std::string_view::npos) {
			return components;
		}
		rest.remove_prefix(end + 1);
	}
}

bool IsProgramExtension(const std::string_view ext)
{
	return std::find(ProgramExtensions.begin(), ProgramExtensions.end(), ext) !=
	       ProgramExtensions.end();
}

// The engine's own probes of a drive are never answered from APPEND's list.
std::vector<std::string> RootPrograms(const char drive)
{
	AppendCommand::Suspend no_append_search;
	std::vector<std::string> programs = {};
	TempDtaSearch search              = {};
	auto entry = search.First(std::string(1, drive) + ":\\*.*");
	while (entry) {
		if (entry->IsFile() && IsProgramExtension(entry->GetExtension())) {
			programs.push_back(entry->name);
		}
		entry = search.Next();
	}
	std::sort(programs.begin(), programs.end());
	return programs;
}

std::optional<DOS_DTA::Result> FindExact(const std::string& path)
{
	AppendCommand::Suspend no_append_search;
	TempDtaSearch search = {};
	return search.First(path);
}

std::vector<std::string> LinesFor(const char drive,
                                  const std::vector<std::string>& folders,
                                  const std::string& program)
{
	std::vector<std::string> lines = {std::string(1, drive) + ":"};
	auto folder_path               = std::string();
	for (const auto& folder : folders) {
		folder_path += "\\" + folder;
	}
	if (!folder_path.empty()) {
		lines.push_back("CD " + folder_path);
	}
	// The full path: the shell would take a bare TYPE.EXE's stem for its
	// TYPE command.
	const auto full_path = std::string(1, drive) + ":" + folder_path +
	                       "\\" + program;
	const auto is_batch = program.ends_with(".BAT");
	lines.push_back(is_batch ? "CALL " + full_path : full_path);
	return lines;
}

Target Refused(const Refusal refusal, std::vector<std::string> candidates = {})
{
	Target target     = {};
	target.refusal    = refusal;
	target.candidates = std::move(candidates);
	return target;
}

Target ResolveNamed(const char drive, const std::string_view name)
{
	auto components = SplitName(name);
	if (components.empty()) {
		return Refused(Refusal::BadName);
	}
	const auto leaf = components.back();
	components.pop_back();

	auto folder_path = std::string(1, drive) + ":\\";
	for (const auto& folder : components) {
		folder_path += folder + "\\";
	}

	auto program = std::string();
	if (leaf.find('.') != std::string::npos) {
		const auto entry = FindExact(folder_path + leaf);
		if (!entry) {
			return Refused(Refusal::NotFound, RootPrograms(drive));
		}
		const auto ext = leaf.substr(leaf.find('.') + 1);
		if (!entry->IsFile() || !IsProgramExtension(ext)) {
			return Refused(Refusal::NotAProgram, RootPrograms(drive));
		}
		program = leaf;
	} else {
		for (const auto ext : ProgramExtensions) {
			const auto candidate = leaf + "." + std::string(ext);
			const auto entry = FindExact(folder_path + candidate);
			if (entry && entry->IsFile()) {
				program = candidate;
				break;
			}
		}
		if (program.empty()) {
			const auto refusal = FindExact(folder_path + leaf)
			                           ? Refusal::NotAProgram
			                           : Refusal::NotFound;
			return Refused(refusal, RootPrograms(drive));
		}
	}

	Target target = {};
	target.lines  = LinesFor(drive, components, program);
	return target;
}

} // namespace

Target Resolve(const char drive, const std::string_view name)
{
	const auto letter = AsciiUpper(std::string_view(&drive, 1)).front();
	if (letter < 'A' || letter > 'Z' || !Drives.at(drive_index(letter))) {
		return Refused(Refusal::NoDrive);
	}
	if (!name.empty()) {
		return ResolveNamed(letter, name);
	}

	auto programs = RootPrograms(letter);
	if (programs.size() == 1) {
		Target target = {};
		target.lines  = LinesFor(letter, {}, programs.front());
		return target;
	}
	const auto refusal = programs.empty() ? Refusal::NoProgram
	                                      : Refusal::SeveralPrograms;
	return Refused(refusal, std::move(programs));
}

bool IsProgramName(const std::string_view name)
{
	return !SplitName(name).empty();
}

const Texts& EnglishTexts()
{
	static const Texts texts = {
	        "%s is not a program name on the drive: use an 8.3 path without "
	        "a drive letter.\n",
	        "%s was not found on drive %c:.\n",
	        "%s is not a .COM, .EXE or .BAT file.\n",
	        "Drive %c: has no .COM, .EXE or .BAT file at its root.\n",
	        "Drive %c: has several programs at its root; name one with -run "
	        "NAME.\n",
	        "Drive %c: is not mounted.\n",
	        "Programs at the root: %s\n",
	};
	return texts;
}

namespace {

// A name typed by the user goes back to the DOS screen: no control
// characters (an escape sequence would be obeyed) and a bounded length.
std::string Shown(const std::string_view name)
{
	std::string shown = {};
	for (const char c : name.substr(0, MaxNameLength)) {
		const auto byte = static_cast<uint8_t>(c);
		shown += (byte < 0x20 || byte >= 0x7f) ? '?' : c;
	}
	if (name.size() > MaxNameLength) {
		shown += "...";
	}
	return shown;
}

} // namespace

std::string Describe(const Target& target, const std::string_view name,
                     const char drive, const Texts& texts)
{
	const auto shown = Shown(name);
	auto text        = std::string();
	switch (target.refusal) {
	case Refusal::None: return {};
	case Refusal::NoDrive: text = format_str(texts.no_drive, drive); break;
	case Refusal::BadName:
		text = format_str(texts.bad_name, shown.c_str());
		break;
	case Refusal::NotFound:
		text = format_str(texts.not_found, shown.c_str(), drive);
		break;
	case Refusal::NotAProgram:
		text = format_str(texts.not_a_program, shown.c_str());
		break;
	case Refusal::NoProgram:
		text = format_str(texts.no_program, drive);
		break;
	case Refusal::SeveralPrograms:
		text = format_str(texts.several_programs, drive);
		break;
	}
	if (!target.candidates.empty()) {
		auto names = std::string();
		for (const auto& candidate : target.candidates) {
			names += (names.empty() ? "" : " ") + Shown(candidate);
		}
		text += format_str(texts.candidates, names.c_str());
	}
	return text;
}

} // namespace MountRun
