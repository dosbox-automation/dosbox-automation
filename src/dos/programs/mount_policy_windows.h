// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_PROGRAM_MOUNT_POLICY_WINDOWS_H
#define DOSBOX_PROGRAM_MOUNT_POLICY_WINDOWS_H

#if defined(WIN32)

#include <filesystem>
#include <string>
#include <vector>

#include "utils/env_utils.h"

namespace MountPolicy {

// Literal baseline unioned with the environment (BuildWindowsSystemPaths)
// so a lying or unset variable never unblocks the standard install paths.
inline const std::vector<std::filesystem::path>& SystemPaths()
{
	static const auto paths = BuildWindowsSystemPaths(
	        [](const char* var) { return get_env_var(var); });
	return paths;
}

// IsDeviceNamespacePath declared in mount_policy.h, defined in mount_policy.cpp

} // namespace MountPolicy

#endif // WIN32
#endif // DOSBOX_PROGRAM_MOUNT_POLICY_WINDOWS_H
