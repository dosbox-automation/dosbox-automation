// SPDX-FileCopyrightText:  2024-2025 The DOSBox Staging Team
// SPDX-FileCopyrightText:  2026 dosbox-automation Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_PROGRAM_MODE_H
#define DOSBOX_PROGRAM_MODE_H

#include "dos/dos.h"
#include "dos/programs.h"
#include "dosbox.h"

#include <cstdint>
#include <optional>
#include <string>

// Maps a lowercase MODE symbolic name (bw40, co40, bw80, co80, mono) to
// the BIOS mode number the adapter can actually set. Empty when the
// adapter has no such mode.
std::optional<uint16_t> ResolveSymbolicVideoMode(const std::string& name,
                                                 MachineType machine_type);

class MODE final : public Program {
public:
	MODE()
	{
		AddMessages();
		help_detail = {HELP_Filter::All,
		               HELP_Category::Misc,
		               HELP_CmdType::Program,
		               "MODE"};
	}
	void Run() override;

private:
	bool HandleSetTypematicRate();

	void HandleSetDisplayMode();
	void SetDisplayMode(const std::string& mode_str);

	static void AddMessages();
};

#endif
