// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_MENU_SESSION_H
#define DOSBOX_MENU_SESSION_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dos/programs/archive_chooser.h"

// The menu the engine handed UniMenu, kept so a choice coming back
// through the ports can only start an entry the engine offered.
namespace MenuSession {

inline constexpr std::string_view MenuProgram = "Y:\\DOS\\UNIMENU.EXE";

// True only while Y: is the bundled resource drive and holds UniMenu, so
// the menu turn never goes to a program someone else put at that path.
bool MenuProgramAvailable();

// A later session replaces this one. With exit_after a final choice ends
// with EXIT, as MOUNT -run -exit does after its program.
void Open(char drive, ArchiveChooser::Menu menu, bool exit_after = false);

void Close();

// False once closed, or once the drive letter holds another drive.
bool IsOpen();

// The serialized menu, served byte by byte through the data port.
void Rewind();
std::optional<uint8_t> NextByte();
size_t Remaining();

// The shell lines that start entry `index`: MOUNT -run's lines, then
// the way back to UniMenu for setup and conf entries. Empty when the
// session is closed or the entry does not resolve.
std::vector<std::string> LinesFor(size_t index);

// Queues the lines of entry `index` in the shell. A final choice closes
// the session; a setup entry keeps it for UniMenu's return.
bool Choose(size_t index);

} // namespace MenuSession

#endif
