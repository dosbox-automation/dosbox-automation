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

// Built from environment variables so the correct paths are blocked
// regardless of which drive Windows is installed on.
inline const std::vector<std::filesystem::path>& SystemPaths()
{
	static const auto paths = []() {
		auto result = std::vector<std::filesystem::path>{};

		auto add = [&](const std::string& p) {
			if (!p.empty()) {
				result.push_back(p);
			}
		};

		// %SYSTEMROOT% is the Windows directory (e.g. D:\Windows)
		const auto sysroot = get_env_var("SYSTEMROOT");
		add(sysroot);
		if (!sysroot.empty()) {
			add(sysroot + "\\System32");
			add(sysroot + "\\SysWOW64");
		}

		add(get_env_var("ProgramFiles"));
		add(get_env_var("ProgramFiles(x86)"));
		add(get_env_var("ProgramData"));

		// Recovery partition on the system drive
		const auto sysdrive = get_env_var("SYSTEMDRIVE");
		if (!sysdrive.empty()) {
			add(sysdrive + "\\Recovery");
		}

		return result;
	}();
	return paths;
}

// WinObj device-namespace prefixes that must be rejected outright.
// These never represent real filesystem paths.
inline bool IsDeviceNamespacePath(const std::string& path)
{
	if (path.size() < 4) {
		return false;
	}
	// \\.\  and \\?\
	return (path[0] == '\\' && path[1] == '\\' &&
	        (path[2] == '.' || path[2] == '?') && path[3] == '\\');
}

} // namespace MountPolicy

#endif // WIN32
#endif // DOSBOX_PROGRAM_MOUNT_POLICY_WINDOWS_H
