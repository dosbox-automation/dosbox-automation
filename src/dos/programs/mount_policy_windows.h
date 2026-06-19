// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_PROGRAM_MOUNT_POLICY_WINDOWS_H
#define DOSBOX_PROGRAM_MOUNT_POLICY_WINDOWS_H

#if defined(WIN32)

#include <filesystem>
#include <string>
#include <vector>

namespace MountPolicy {

// Paths that must never be mountable as DOS drives regardless of
// configuration. Each entry blocks the directory itself and everything
// below it. Checked after canonicalization, so symlinks into these
// trees are caught.
//
// Drive-letter roots (C:\, D:\, ...) are rejected separately by the
// bare-root check in IsUnderSystemPath, not listed here.
inline const std::vector<std::filesystem::path>& SystemPaths()
{
	static const std::vector<std::filesystem::path> paths = {
	        "C:\\Windows",
	        "C:\\Windows\\System32",
	        "C:\\Windows\\SysWOW64",
	        "C:\\Program Files",
	        "C:\\Program Files (x86)",
	        "C:\\ProgramData",
	        "C:\\Recovery",
	};
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
