// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "shell/shell_args.h"

#include "utils/checks.h"
#include "utils/string_utils.h"

CHECK_NARROWING();

namespace ShellArgs {

namespace {

constexpr std::string_view Blanks = " \t";

std::string_view Trimmed(std::string_view text, const std::string_view leading)
{
	const auto start = text.find_first_not_of(leading);
	if (start == std::string_view::npos) {
		return {};
	}
	text.remove_prefix(start);
	text.remove_suffix(text.size() - 1 - text.find_last_not_of(Blanks));
	return text;
}

} // namespace

OnOff ParseOnOff(const std::string_view line)
{
	// The separators COMMAND.COM reads.
	const auto word = Trimmed(line, " \t,;=");
	if (word.empty()) {
		return OnOff::Show;
	}
	if (iequals(word, "on")) {
		return OnOff::On;
	}
	if (iequals(word, "off")) {
		return OnOff::Off;
	}
	return OnOff::Invalid;
}

std::optional<std::string> ParseDeviceName(const std::string_view line)
{
	const auto word = Trimmed(line, Blanks);
	if (word.empty() || word.find_first_of(Blanks) != std::string_view::npos) {
		return {};
	}
	return std::string(word);
}

} // namespace ShellArgs
