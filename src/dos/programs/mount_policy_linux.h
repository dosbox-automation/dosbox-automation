// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_PROGRAM_MOUNT_POLICY_LINUX_H
#define DOSBOX_PROGRAM_MOUNT_POLICY_LINUX_H

#if !defined(WIN32)

#include <filesystem>
#include <vector>

namespace MountPolicy {

// Paths that must never be mountable as DOS drives regardless of
// configuration. Each entry blocks the directory itself and everything
// below it. Checked after canonicalization, so symlinks into these
// trees are caught.
inline const std::vector<std::filesystem::path>& SystemPaths()
{
	static const std::vector<std::filesystem::path> paths = {
	        "/",
	        "/bin",
	        "/boot",
	        "/dev",
	        "/etc",
	        "/lib",
	        "/lib32",
	        "/lib64",
	        "/libx32",
	        "/proc",
	        "/root",
	        "/run",
	        "/sbin",
	        "/snap",
	        "/sys",
	        "/usr",
	        "/var",
	};
	return paths;
}

} // namespace MountPolicy

#endif // !WIN32
#endif // DOSBOX_PROGRAM_MOUNT_POLICY_LINUX_H
