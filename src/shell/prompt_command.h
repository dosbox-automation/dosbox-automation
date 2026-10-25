// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_SHELL_PROMPT_COMMAND_H
#define DOSBOX_SHELL_PROMPT_COMMAND_H

#include <functional>
#include <string>
#include <string_view>

namespace PromptCommand {

// The values a prompt can show; each is asked for only when the prompt
// text uses its code.
struct Context {
	std::function<char()> drive             = {};
	std::function<std::string()> drive_path = {};
	std::function<std::string()> time       = {};
	std::function<std::string()> date       = {};
	std::function<std::string()> version    = {};
};

// MS-DOS 5.0 User's Guide, PROMPT: the $ codes, case-insensitive.
std::string Render(std::string_view prompt, const Context& context);

// The text PROMPT sets; empty means back to the default prompt.
std::string ParseArguments(std::string_view line);

} // namespace PromptCommand

#endif
