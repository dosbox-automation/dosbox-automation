// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "mount_policy_system_paths.h"

#include "dosbox.h"
#include "mount_policy.h"

#include <cassert>
#include <cctype>

#include "utils/checks.h"
#include "utils/env_utils.h"

CHECK_NARROWING();

namespace MountPolicy {

namespace {

struct Row {
	HostOs os;
	bool takes_common_unix;
	std::vector<std::filesystem::path> entries;
	std::vector<std::filesystem::path> carve_outs;
	Ancestors ancestors;
};

// Rows hold only what the common list lacks. Sources per OS: hier(7)
// and Apple's files project, developer-docs reference
// dosbox-automation/system-dirs-per-os.
const std::vector<Row>& Table()
{
	// clang-format off
	// /opt/games is the one carve-out (D2); everything else under /opt
	// stays blocked, Homebrew's /opt/homebrew on Apple Silicon included
	static const std::vector<std::filesystem::path> unix_carve_outs = {
	        "/opt/games"};
	// /home is a symlink into a blocked tree on some installs (/var/home on
	// ostree Linux, /usr/home on older FreeBSD and DragonFly); this opens the
	// canonical spelling, a path through the symlink is still refused as such
	static const std::vector<std::filesystem::path> linux_carve_outs = {
	        "/opt/games", "/var/home"};
	static const std::vector<std::filesystem::path> bsd_home_carve_outs = {
	        "/opt/games", "/usr/home"};

	static const std::vector<Row> rows = {
	        {HostOs::Linux, true,
	         {"/lib32", "/lib64", "/libx32", "/run", "/snap", "/sys"},
	         linux_carve_outs, Ancestors::Blocked},

	        // Entries come from BuildWindowsSystemPaths (a fixed list plus the
	        // environment), so this row carries only the rules
	        {HostOs::Windows, false, {}, {}, Ancestors::Open},

	        // /etc, /var and /tmp are symlinks into /private, so the common
	        // entries never match a canonical path there; /private/var is
	        // enumerated (D1), folders and tmp left open (macOS 26.6 listing, P1)
	        {HostOs::MacOs, true,
	         {"/System",
	          "/Library",
	          "/Applications",
	          "/cores",
	          "/AppleInternal",
	          "/private/etc",
	          "/private/tftpboot",
	          "/private/var/agentx",
	          "/private/var/at",
	          "/private/var/audit",
	          "/private/var/backups",
	          "/private/var/boot",
	          "/private/var/containers",
	          "/private/var/db",
	          "/private/var/dextcores",
	          "/private/var/dirs_cleaner",
	          "/private/var/empty",
	          "/private/var/install",
	          "/private/var/jabberd",
	          "/private/var/lib",
	          "/private/var/log",
	          "/private/var/ma",
	          "/private/var/mail",
	          "/private/var/msgs",
	          "/private/var/netboot",
	          "/private/var/networkd",
	          "/private/var/OOPJit",
	          "/private/var/protected",
	          "/private/var/root",
	          "/private/var/rpc",
	          "/private/var/run",
	          "/private/var/select",
	          "/private/var/sntpd",
	          "/private/var/spool",
	          "/private/var/vm",
	          "/private/var/yp"},
	         unix_carve_outs, Ancestors::Blocked},

	        // GhostBSD reports itself as FreeBSD
	        {HostOs::FreeBsd, true,
	         {"/libexec", "/rescue", "/compat"},
	         bsd_home_carve_outs, Ancestors::Blocked},

	        {HostOs::NetBsd, true,
	         {"/libexec", "/rescue", "/altroot", "/kern", "/libdata", "/stand", "/netbsd"},
	         unix_carve_outs, Ancestors::Blocked},

	        {HostOs::OpenBsd, true,
	         {"/altroot", "/bsd", "/bsd.mp", "/bsd.rd", "/bsd.sp"},
	         unix_carve_outs, Ancestors::Blocked},

	        // Not researched beyond hier(7)'s common ground; the common list
	        // only until someone with the system adds its own directories
	        {HostOs::DragonFly, true, {}, bsd_home_carve_outs, Ancestors::Blocked},
	};
	// clang-format on
	return rows;
}

const Row* FindRow(const HostOs os)
{
	for (const auto& row : Table()) {
		if (row.os == os) {
			return &row;
		}
	}
	return nullptr;
}

} // namespace

const std::vector<std::filesystem::path>& CommonUnixSystemPaths()
{
	static const std::vector<std::filesystem::path> paths = {
	        "/",
	        "/bin",
	        "/boot",
	        "/dev",
	        "/etc",
	        "/lib",
	        "/opt",
	        "/proc",
	        "/root",
	        "/sbin",
	        "/usr",
	        "/var",
	};
	return paths;
}

std::vector<std::filesystem::path> SystemPathsFor(const HostOs os)
{
	if (os == HostOs::Windows) {
		return BuildWindowsSystemPaths(
		        [](const char* var) { return get_env_var(var); });
	}

	const auto* row = FindRow(os);
	assert(row != nullptr);

	std::vector<std::filesystem::path> paths = {};
	if (row == nullptr || row->takes_common_unix) {
		paths = CommonUnixSystemPaths();
	}
	if (row != nullptr) {
		paths.insert(paths.end(), row->entries.begin(), row->entries.end());
	}
	return paths;
}

const std::vector<std::filesystem::path>& CarveOutsFor(const HostOs os)
{
	static const std::vector<std::filesystem::path> none = {};

	const auto* row = FindRow(os);
	return row != nullptr ? row->carve_outs : none;
}

const std::vector<std::filesystem::path>& SystemPaths()
{
	static const auto paths = SystemPathsFor(CurrentHostOs());
	return paths;
}

const std::vector<std::filesystem::path>& CarveOuts()
{
	static const auto& carve_outs = CarveOutsFor(CurrentHostOs());
	return carve_outs;
}

Ancestors AncestorsFor(const HostOs os)
{
	const auto* row = FindRow(os);
	return row != nullptr ? row->ancestors : Ancestors::Blocked;
}

bool PathIsAtOrBelow(const std::string& path, const std::string& prefix)
{
	if (PathEquals(path, prefix)) {
		return true;
	}
	// "/etc" covers "/etc/shadow" but not "/etcetera"
	if (path.size() > prefix.size() && PathStartsWith(path, prefix)) {
		const auto next = path[prefix.size()];
#if defined(WIN32) || defined(_WIN32)
		return next == '\\' || next == '/';
#else
		return next == '/';
#endif
	}
	return false;
}

bool IsBareRoot(const std::filesystem::path& canonical_path)
{
#if defined(WIN32) || defined(_WIN32)
	// "C:\", "D:\", etc.
	const auto s = canonical_path.string();
	return (s.size() == 3 && std::isalpha(static_cast<unsigned char>(s[0])) &&
	        s[1] == ':' && (s[2] == '\\' || s[2] == '/'));
#else
	return canonical_path == "/";
#endif
}

bool PathStartsWith(const std::string& path, const std::string& prefix)
{
	if (path.size() < prefix.size()) {
		return false;
	}
#if defined(WIN32) || defined(_WIN32)
	for (size_t i = 0; i < prefix.size(); ++i) {
		if (std::tolower(static_cast<unsigned char>(path[i])) !=
		    std::tolower(static_cast<unsigned char>(prefix[i]))) {
			return false;
		}
	}
	return true;
#else
	return path.compare(0, prefix.size(), prefix) == 0;
#endif
}

bool PathEquals(const std::string& a, const std::string& b)
{
#if defined(WIN32) || defined(_WIN32)
	if (a.size() != b.size()) {
		return false;
	}
	return PathStartsWith(a, b);
#else
	return a == b;
#endif
}

bool IsUnderSystemPathIn(const std::filesystem::path& canonical_path,
                         const std::vector<std::filesystem::path>& entries,
                         const std::vector<std::filesystem::path>& carve_outs,
                         const Ancestors ancestors)
{
	const auto canonical_str = canonical_path.string();

	for (const auto& carve_out : carve_outs) {
		if (PathIsAtOrBelow(canonical_str, carve_out.string())) {
			return false;
		}
	}

	if (IsBareRoot(canonical_path)) {
		return true;
	}

	for (const auto& entry : entries) {
		const auto entry_str = entry.string();
		if (IsBareRoot(entry)) {
			continue;
		}
		if (PathIsAtOrBelow(canonical_str, entry_str)) {
			return true;
		}
		// A mount of an ancestor hands the guest the entry as a
		// subdirectory
		if (ancestors == Ancestors::Blocked &&
		    PathIsAtOrBelow(entry_str, canonical_str)) {
			return true;
		}
	}
	return false;
}

} // namespace MountPolicy
