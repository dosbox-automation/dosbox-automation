// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_SHELL_ARGS_H
#define DOSBOX_SHELL_ARGS_H

#include <optional>
#include <string>
#include <string_view>

namespace ShellArgs {

enum class OnOff { Show, On, Off, Invalid };

// BREAK and VERIFY: nothing shows the state, ON or OFF in any case sets it.
OnOff ParseOnOff(std::string_view line);

// CTTY: exactly one word; nothing or more than one is no device.
std::optional<std::string> ParseDeviceName(std::string_view line);

} // namespace ShellArgs

#endif
