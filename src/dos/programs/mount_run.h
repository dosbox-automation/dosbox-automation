// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#ifndef DOSBOX_MOUNT_RUN_H
#define DOSBOX_MOUNT_RUN_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dos/dos.h"

// Design D8: the program MOUNT -run starts on the drive it just mounted.
namespace MountRun {

std::string AsciiUpper(std::string_view text);

// FindFirst writes into a DTA, so the caller's stays untouched. A nested
// search clobbers the outer walk's state.
class TempDtaSearch {
public:
	TempDtaSearch();
	~TempDtaSearch();
	TempDtaSearch(const TempDtaSearch&)            = delete;
	TempDtaSearch& operator=(const TempDtaSearch&) = delete;

	std::optional<DOS_DTA::Result> First(const std::string& pattern);
	std::optional<DOS_DTA::Result> Next();

private:
	DOS_DTA::Result Current();

	const RealPt saved_dta;
};

enum class Refusal {
	None,
	NoDrive,
	BadName,
	NotFound,
	NotAProgram,
	NoProgram,
	SeveralPrograms,
};

struct Target {
	// The shell lines that start the program; empty on any refusal.
	std::vector<std::string> lines = {};
	// The .COM, .EXE and .BAT files at the root, sorted, when no single
	// program was settled on.
	std::vector<std::string> candidates = {};
	Refusal refusal                     = Refusal::None;
};

// A given name is an 8.3 path on the drive, without a drive letter and
// with no extension meaning .COM, .EXE, .BAT in that order. No name takes
// the only program at the root.
Target Resolve(char drive, std::string_view name);

// The name rule of Resolve alone, for callers that write the name into a
// command line before any drive exists.
bool IsProgramName(std::string_view name);

// Format strings for the refusals; MOUNT fills them from its messages,
// whose English text is EnglishTexts().
struct Texts {
	std::string bad_name         = {}; // name
	std::string not_found        = {}; // name, drive
	std::string not_a_program    = {}; // name
	std::string no_program       = {}; // drive
	std::string several_programs = {}; // drive
	std::string no_drive         = {}; // drive
	std::string candidates       = {}; // the names, space separated
};

const Texts& EnglishTexts();

// What MOUNT shows for a target that runs nothing; empty for one that runs.
std::string Describe(const Target& target, std::string_view name, char drive,
                     const Texts& texts);

} // namespace MountRun

#endif
