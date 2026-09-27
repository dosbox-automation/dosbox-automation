// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_PROGRAM_MOUNT_POLICY_SYSTEM_PATHS_H
#define DOSBOX_PROGRAM_MOUNT_POLICY_SYSTEM_PATHS_H

#include <filesystem>
#include <string>
#include <vector>

namespace MountPolicy {

enum class HostOs {
	Linux,
	Windows,
	MacOs,
	FreeBsd,
	NetBsd,
	OpenBsd,
	DragonFly,
};

// One detection chain for the whole policy; an OS without a row here
// stops the build (design 2026-09-25-system-path-table, D3)
constexpr HostOs CurrentHostOs()
{
#if defined(WIN32) || defined(_WIN32)
	return HostOs::Windows;
#elif defined(__APPLE__)
	return HostOs::MacOs;
#elif defined(__FreeBSD__)
	return HostOs::FreeBsd;
#elif defined(__NetBSD__)
	return HostOs::NetBsd;
#elif defined(__OpenBSD__)
	return HostOs::OpenBsd;
#elif defined(__DragonFly__)
	return HostOs::DragonFly;
#elif defined(__linux__)
	return HostOs::Linux;
#else
#error "Unknown host OS: add it to HostOs and the system path table, see docs/PORTING.md"
#endif
}

// Whether a mount of a directory that contains an entry is refused too.
// Unix rows block them (/private on macOS would expose /private/etc);
// the Windows list carries the user's temp directory, so its parents stay open
enum class Ancestors { Blocked, Open };

// Directories every Unix row blocks; a row opts in (D4)
const std::vector<std::filesystem::path>& CommonUnixSystemPaths();

// The blocked list for one OS: the common list where the row takes it,
// plus the row's own entries. Windows reads its list from the environment.
std::vector<std::filesystem::path> SystemPathsFor(HostOs os);

// Exact subtrees a row opens again inside a blocked entry (D2)
const std::vector<std::filesystem::path>& CarveOutsFor(HostOs os);

Ancestors AncestorsFor(HostOs os);

// The build host's own list and carve-outs, computed once
const std::vector<std::filesystem::path>& SystemPaths();
const std::vector<std::filesystem::path>& CarveOuts();

// The comparison IsUnderSystemPath() runs, against any row: the bare root
// matches only itself, other entries match exactly or as a prefix followed
// by a separator, ancestors per the row's rule, a carve-out is never blocked
bool IsUnderSystemPathIn(const std::filesystem::path& canonical_path,
                         const std::vector<std::filesystem::path>& entries,
                         const std::vector<std::filesystem::path>& carve_outs,
                         Ancestors ancestors);

// Path string helpers shared with the whitelist check; case-insensitive
// on Windows, where C:\Windows and c:\windows are one directory
bool IsBareRoot(const std::filesystem::path& canonical_path);
bool PathStartsWith(const std::string& path, const std::string& prefix);
bool PathEquals(const std::string& a, const std::string& b);

// True when path equals prefix or lies below it: "/etc" covers
// "/etc/shadow" but not "/etcetera"; shared by the denylist and the whitelist
bool PathIsAtOrBelow(const std::string& path, const std::string& prefix);

} // namespace MountPolicy

#endif // DOSBOX_PROGRAM_MOUNT_POLICY_SYSTEM_PATHS_H
