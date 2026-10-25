// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "shell/prompt_command.h"

#include "utils/checks.h"

#include <cctype>

CHECK_NARROWING();

namespace PromptCommand {

namespace {

std::string Ask(const std::function<std::string()>& value)
{
	return value ? value() : std::string();
}

} // namespace

std::string Render(const std::string_view prompt, const Context& context)
{
	std::string text = {};
	for (size_t i = 0; i < prompt.size(); ++i) {
		if (prompt[i] != '$') {
			text += prompt[i];
			continue;
		}
		// MS-DOS 4.0 TCMD2A.ASM print_prompt: a $ at the end ends the
		// text, a code it does not know is skipped.
		if (++i == prompt.size()) {
			break;
		}
		switch (std::toupper(static_cast<unsigned char>(prompt[i]))) {
		case 'Q': text += '='; break;
		case '$': text += '$'; break;
		case 'T': text += Ask(context.time); break;
		case 'D': text += Ask(context.date); break;
		case 'P': text += Ask(context.drive_path); break;
		case 'V': text += Ask(context.version); break;
		case 'N':
			if (context.drive) {
				text += context.drive();
			}
			break;
		case 'G': text += '>'; break;
		case 'L': text += '<'; break;
		case 'B': text += '|'; break;
		case '_': text += "\r\n"; break;
		case 'E': text += '\x1b'; break;
		case 'H': text += "\b \b"; break;
		default: break;
		}
	}
	return text;
}

std::string ParseArguments(const std::string_view line)
{
	auto start = line.find_first_not_of(" \t");
	if (start != std::string_view::npos && line[start] == '=') {
		start = line.find_first_not_of(" \t", start + 1);
	}
	if (start == std::string_view::npos) {
		return {};
	}
	return std::string(line.substr(start));
}

} // namespace PromptCommand
