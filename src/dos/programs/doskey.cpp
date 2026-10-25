// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "doskey.h"

#include "misc/messages.h"
#include "misc/unicode.h"
#include "more_output.h"
#include "shell/shell.h"
#include "utils/checks.h"
#include "utils/string_utils.h"

#include <algorithm>
#include <cctype>

CHECK_NARROWING();

extern unsigned int result_errorcode;

namespace DoskeyCommand {

namespace {

bool IsBufferSize(const std::string& arg)
{
	constexpr std::string_view Prefix = "/BUFSIZE=";
	if (arg.size() <= Prefix.size() ||
	    !iequals(std::string_view(arg).substr(0, Prefix.size()), Prefix)) {
		return false;
	}
	return std::all_of(arg.begin() + Prefix.size(), arg.end(), [](const char c) {
		return std::isdigit(static_cast<unsigned char>(c));
	});
}

} // namespace

std::variant<IllegalSwitch, Request> ParseArguments(const std::vector<std::string>& args)
{
	Request request = {};
	for (size_t i = 0; i < args.size(); ++i) {
		const auto& arg = args[i];
		if (arg.empty() || arg.front() != '/') {
			std::string text = arg;
			for (size_t j = i + 1; j < args.size(); ++j) {
				text += " " + args[j];
			}
			request.macro_text = text;
			break;
		}
		if (iequals(arg, "/HISTORY") || iequals(arg, "/H")) {
			request.show_history = true;
		} else if (iequals(arg, "/MACROS") || iequals(arg, "/M")) {
			request.show_macros = true;
		} else if (!iequals(arg, "/REINSTALL") && !iequals(arg, "/INSERT") &&
		           !iequals(arg, "/OVERSTRIKE") && !IsBufferSize(arg)) {
			return IllegalSwitch{arg};
		}
	}
	return request;
}

} // namespace DoskeyCommand

void DOSKEY::Run()
{
	if (HelpRequested()) {
		MoreOutputStrings output(*this);
		output.AddString(MSG_Get("PROGRAM_DOSKEY_HELP_LONG"));
		output.Display();
		return;
	}

	const auto parsed = DoskeyCommand::ParseArguments(cmd->GetArguments());
	if (const auto error = std::get_if<DoskeyCommand::IllegalSwitch>(&parsed)) {
		WriteOut(MSG_Get("SHELL_ILLEGAL_SWITCH"), error->argument.c_str());
		result_errorcode = 1;
		return;
	}
	const auto& request = std::get<DoskeyCommand::Request>(parsed);
	if (request.macro_text) {
		WriteOut(MSG_Get("PROGRAM_DOSKEY_NO_MACROS"));
		result_errorcode = 1;
		return;
	}
	if (request.show_history) {
		if (const auto shell = DOS_GetFirstShell()) {
			for (const auto& line :
			     shell->HistoryCommands(get_utf8_code_page())) {
				WriteOut_NoParsing(line + "\n");
			}
		}
	}
	// No macros can be defined, so /MACROS has nothing to list.
	if (request.show_history || request.show_macros) {
		return;
	}
	// MS-DOS 5.0 says so once, when DOSKEY goes resident.
	static bool announced = false;
	if (!announced) {
		WriteOut(MSG_Get("PROGRAM_DOSKEY_INSTALLED"));
		announced = true;
	}
}

void DOSKEY::AddMessages()
{
	if (MSG_Exists("PROGRAM_DOSKEY_HELP")) {
		return;
	}
	MSG_Add("PROGRAM_DOSKEY_HELP",
	        "Command history and line editing, built into the shell.\n");
	MSG_Add("PROGRAM_DOSKEY_HELP_LONG",
	        "Command history and line editing, built into the shell.\n"
	        "\n"
	        "Usage:\n"
	        "  [color=light-green]doskey[reset] [/reinstall] [/bufsize=size] [/insert] [/overstrike]\n"
	        "  [color=light-green]doskey[reset] /history\n"
	        "  [color=light-green]doskey[reset] /macros\n"
	        "\n"
	        "Notes:\n"
	        "  - The shell keeps its own history (Up, Down, F3), so [color=light-green]doskey[reset] has\n"
	        "    nothing to load; the switches of MS-DOS 5.0 are accepted and change nothing.\n"
	        "  - [color=light-green]doskey[reset] /history (or /h) lists the commands typed so far.\n"
	        "  - Macros ([color=light-cyan]name=text[reset]) are not supported; /macros lists none.\n");
	MSG_Add("PROGRAM_DOSKEY_INSTALLED",
	        "DOSKey installed (the command history is built into the shell).\n");
	MSG_Add("PROGRAM_DOSKEY_NO_MACROS", "DOSKEY macros are not supported.\n");
}
