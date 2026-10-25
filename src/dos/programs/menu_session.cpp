// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "dos/programs/menu_session.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <string_view>
#include <typeinfo>

#include "augra/log.h"
#include "dos/dos.h"
#include "dos/drives.h"
#include "dos/programs/mount_run.h"
#include "misc/support.h"
#include "shell/shell.h"
#include "utils/checks.h"

CHECK_NARROWING();

namespace MenuSession {

namespace {

struct State {
	bool open       = false;
	bool exit_after = false;
	char drive      = '\0';
	// Expires with the drive, so a new drive at the same address differs.
	std::weak_ptr<DOS_Drive> drive_seen = {};
	ArchiveChooser::Menu menu           = {};
	std::string data                    = {};
	size_t position                     = 0;
};

State state = {};

std::shared_ptr<DOS_Drive> DriveAt(const char letter)
{
	if (letter < 'A' || letter > 'Z') {
		return {};
	}
	return Drives.at(drive_index(letter));
}

constexpr char ResourceDrive = 'Y';

bool ReturnsToMenu(const ArchiveChooser::Category category)
{
	return category == ArchiveChooser::Category::Setup ||
	       category == ArchiveChooser::Category::Conf;
}

} // namespace

bool MenuProgramAvailable()
{
	// An overlay is a localDrive too; only the plain bundled one counts.
	const auto drive_now = DriveAt(ResourceDrive);
	if (!drive_now || typeid(*drive_now) != typeid(localDrive)) {
		return false;
	}
	const auto* drive  = static_cast<const localDrive*>(drive_now.get());
	const auto bundled = get_resource_path("drives", "y");
	std::error_code ec = {};
	if (bundled.empty() ||
	    !std_fs::equivalent(drive->GetBasedir(), bundled, ec) || ec) {
		return false;
	}
	return std_fs::is_regular_file(bundled / "dos" / "unimenu.exe", ec);
}

void Open(const char drive, ArchiveChooser::Menu menu, const bool exit_after)
{
	const auto letter = static_cast<char>(
	        std::toupper(static_cast<unsigned char>(drive)));
	state                = {};
	const auto drive_now = DriveAt(letter);
	if (!drive_now) {
		return;
	}
	state.drive_seen = drive_now;
	state.open       = true;
	state.exit_after = exit_after;
	state.drive      = letter;
	state.data       = ArchiveChooser::SerializeMenu(menu);
	state.menu       = std::move(menu);
}

void Close()
{
	state = {};
}

bool IsOpen()
{
	const auto seen = state.drive_seen.lock();
	if (state.open && (!seen || DriveAt(state.drive) != seen)) {
		Close();
	}
	return state.open;
}

void Rewind()
{
	state.position = 0;
}

std::optional<uint8_t> NextByte()
{
	if (!IsOpen() || state.position >= state.data.size()) {
		return {};
	}
	return static_cast<uint8_t>(state.data[state.position++]);
}

size_t Remaining()
{
	return IsOpen() ? state.data.size() - state.position : 0;
}

std::vector<std::string> LinesFor(const size_t index)
{
	if (!IsOpen() || index >= state.menu.entries.size()) {
		return {};
	}
	const auto& entry = state.menu.entries[index];
	if (!ArchiveChooser::IsValidArgs(entry.args)) {
		augra::log_warn("menu", "entry %zu refused: unsafe arguments", index);
		return {};
	}
	auto target = MountRun::Resolve(state.drive, entry.exe);
	if (target.lines.empty()) {
		augra::log_warn("menu",
		                "entry %zu refused: no program on drive %c:",
		                index,
		                state.drive);
		return {};
	}
	if (!entry.args.empty()) {
		target.lines.back() += " " + entry.args;
	}
	if (ReturnsToMenu(entry.category)) {
		if (MenuProgramAvailable()) {
			target.lines.emplace_back("CD \\");
			target.lines.emplace_back(MenuProgram);
		}
	} else if (state.exit_after) {
		target.lines.emplace_back("EXIT");
	}
	return target.lines;
}

bool Choose(const size_t index)
{
	auto lines = LinesFor(index);
	if (lines.empty()) {
		return false;
	}
	const auto returns = lines.back() == MenuProgram;
	if (!SHELL_QueuePendingCommands(std::move(lines))) {
		augra::log_warn("menu", "entry %zu not queued: the shell is busy", index);
		return false;
	}
	if (!returns) {
		Close();
	}
	return true;
}

} // namespace MenuSession
