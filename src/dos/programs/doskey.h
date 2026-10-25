// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_PROGRAM_DOSKEY_H
#define DOSBOX_PROGRAM_DOSKEY_H

#include "dos/programs.h"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace DoskeyCommand {

struct Request {
	bool show_history                     = false;
	bool show_macros                      = false;
	std::optional<std::string> macro_text = {};
};

struct IllegalSwitch {
	std::string argument = {};
};

// MS-DOS 5.0 DOSKEY: /REINSTALL /BUFSIZE=n /MACROS /HISTORY /INSERT
// /OVERSTRIKE, then an optional macroname=text.
std::variant<IllegalSwitch, Request> ParseArguments(const std::vector<std::string>& args);

} // namespace DoskeyCommand

class DOSKEY final : public Program {
public:
	DOSKEY()
	{
		AddMessages();
		help_detail = {HELP_Filter::All,
		               HELP_Category::Misc,
		               HELP_CmdType::Program,
		               "DOSKEY"};
	}
	void Run() override;

private:
	void AddMessages();
};

#endif
