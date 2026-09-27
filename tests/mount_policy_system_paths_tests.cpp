// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "dos/programs/mount_policy_system_paths.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <vector>

namespace {

namespace fs = std::filesystem;
using MountPolicy::HostOs;

bool Contains(const std::vector<fs::path>& list, const char* entry)
{
	return std::find(list.begin(), list.end(), fs::path(entry)) != list.end();
}

constexpr HostOs kUnixRows[] = {HostOs::Linux,
                                HostOs::MacOs,
                                HostOs::FreeBsd,
                                HostOs::NetBsd,
                                HostOs::OpenBsd,
                                HostOs::DragonFly};

bool BlockedOn(const HostOs os, const char* path)
{
	return MountPolicy::IsUnderSystemPathIn(fs::path(path),
	                                        MountPolicy::SystemPathsFor(os),
	                                        MountPolicy::CarveOutsFor(os),
	                                        MountPolicy::AncestorsFor(os));
}

// The Linux list as it stood before the table (mount_policy_linux.h,
// 17 entries) plus /opt, which the table blocks on every Unix (D2).
TEST(SystemPathTable, LinuxRowIsTheListFromBeforeTheTablePlusOpt)
{
	const std::vector<fs::path> before = {
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
	        "/opt",
	};

	auto linux_row = MountPolicy::SystemPathsFor(HostOs::Linux);
	std::sort(linux_row.begin(), linux_row.end());

	auto expected = before;
	std::sort(expected.begin(), expected.end());

	EXPECT_EQ(linux_row, expected);
}

TEST(SystemPathTable, CurrentHostRowIsWhatTheCheckUses)
{
	const auto& used = MountPolicy::SystemPaths();
	const auto row = MountPolicy::SystemPathsFor(MountPolicy::CurrentHostOs());

	EXPECT_EQ(used, row);
}

TEST(SystemPathTable, EveryUnixRowTakesTheCommonList)
{
	for (const auto os : kUnixRows) {
		const auto row = MountPolicy::SystemPathsFor(os);
		for (const auto& common : MountPolicy::CommonUnixSystemPaths()) {
			EXPECT_TRUE(std::find(row.begin(), row.end(), common) !=
			            row.end())
			        << common << " missing from row "
			        << static_cast<int>(os);
		}
	}
}

TEST(SystemPathTable, CheckAgainstAGivenRowBlocksEntriesAndTheirChildren)
{
	const std::vector<fs::path> row           = {"/", "/etc", "/usr"};
	const std::vector<fs::path> no_carve_outs = {};

	EXPECT_TRUE(MountPolicy::IsUnderSystemPathIn(
	        "/", row, no_carve_outs, MountPolicy::Ancestors::Blocked));
	EXPECT_TRUE(MountPolicy::IsUnderSystemPathIn(
	        "/etc", row, no_carve_outs, MountPolicy::Ancestors::Blocked));
	EXPECT_TRUE(MountPolicy::IsUnderSystemPathIn(
	        "/etc/shadow", row, no_carve_outs, MountPolicy::Ancestors::Blocked));
	EXPECT_FALSE(MountPolicy::IsUnderSystemPathIn(
	        "/etcetera", row, no_carve_outs, MountPolicy::Ancestors::Blocked));
	EXPECT_FALSE(MountPolicy::IsUnderSystemPathIn(
	        "/home/user", row, no_carve_outs, MountPolicy::Ancestors::Blocked));
}

TEST(SystemPathTable, CarveOutOpensOneExactSubtree)
{
	const std::vector<fs::path> row        = {"/", "/opt"};
	const std::vector<fs::path> carve_outs = {"/opt/games"};

	EXPECT_TRUE(MountPolicy::IsUnderSystemPathIn(
	        "/opt", row, carve_outs, MountPolicy::Ancestors::Blocked));
	EXPECT_TRUE(MountPolicy::IsUnderSystemPathIn(
	        "/opt/docker", row, carve_outs, MountPolicy::Ancestors::Blocked));
	EXPECT_FALSE(MountPolicy::IsUnderSystemPathIn(
	        "/opt/games", row, carve_outs, MountPolicy::Ancestors::Blocked));
	EXPECT_FALSE(MountPolicy::IsUnderSystemPathIn(
	        "/opt/games/doom", row, carve_outs, MountPolicy::Ancestors::Blocked));
	EXPECT_TRUE(MountPolicy::IsUnderSystemPathIn(
	        "/opt/gamesx", row, carve_outs, MountPolicy::Ancestors::Blocked));
}

// Sources: hier(7) of each system and Apple's files project; the
// research is in developer-docs reference/dosbox-automation/system-dirs-per-os.
TEST(SystemPathTable, BsdRowsPinTheirOwnDirectories)
{
	const auto freebsd = MountPolicy::SystemPathsFor(HostOs::FreeBsd);
	for (const auto entry : {"/libexec", "/rescue", "/compat"}) {
		EXPECT_TRUE(Contains(freebsd, entry)) << entry;
	}

	const auto netbsd = MountPolicy::SystemPathsFor(HostOs::NetBsd);
	for (const auto entry :
	     {"/libexec", "/rescue", "/altroot", "/kern", "/libdata", "/stand", "/netbsd"}) {
		EXPECT_TRUE(Contains(netbsd, entry)) << entry;
	}

	const auto openbsd = MountPolicy::SystemPathsFor(HostOs::OpenBsd);
	for (const auto entry :
	     {"/altroot", "/bsd", "/bsd.mp", "/bsd.rd", "/bsd.sp"}) {
		EXPECT_TRUE(Contains(openbsd, entry)) << entry;
	}
}

TEST(SystemPathTable, MacosRowPinsTheSymlinkTargetsUnderPrivate)
{
	// /etc, /var and /tmp are symlinks into /private on macOS, so the
	// canonical path never starts with the common entries (ada-8a9d)
	const auto macos = MountPolicy::SystemPathsFor(HostOs::MacOs);
	for (const auto entry : {"/System",
	                         "/Library",
	                         "/Applications",
	                         "/cores",
	                         "/AppleInternal",
	                         "/private/etc",
	                         "/private/tftpboot",
	                         "/private/var/db",
	                         "/private/var/root",
	                         "/private/var/log",
	                         "/private/var/run",
	                         "/private/var/vm",
	                         "/private/var/audit",
	                         "/private/var/backups",
	                         "/private/var/install",
	                         "/private/var/protected"}) {
		EXPECT_TRUE(Contains(macos, entry)) << entry;
	}

	EXPECT_TRUE(BlockedOn(HostOs::MacOs, "/private/etc/passwd"));
	EXPECT_TRUE(BlockedOn(HostOs::MacOs, "/private/var/db/dslocal"));
	EXPECT_TRUE(BlockedOn(HostOs::MacOs, "/opt/homebrew/bin"));
}

TEST(SystemPathTable, NoUnixRowBlocksTheUserAreas)
{
	for (const auto os : kUnixRows) {
		for (const auto path : {"/home/user/games",
		                        "/Users/user/games",
		                        "/Volumes/Games",
		                        "/tmp/run",
		                        "/mnt/games",
		                        "/media/cdrom",
		                        "/private/tmp/run",
		                        "/private/var/folders/x0/abc/T/pytest-0",
		                        "/private/var/tmp/run"}) {
			EXPECT_FALSE(BlockedOn(os, path))
			        << path << " on row " << static_cast<int>(os);
		}
	}
}

TEST(SystemPathTable, OptIsBlockedWithGamesCarvedOutOnEveryUnixRow)
{
	for (const auto os : kUnixRows) {
		EXPECT_TRUE(BlockedOn(os, "/opt")) << static_cast<int>(os);
		EXPECT_TRUE(BlockedOn(os, "/opt/docker")) << static_cast<int>(os);
		EXPECT_FALSE(BlockedOn(os, "/opt/games")) << static_cast<int>(os);
		EXPECT_FALSE(BlockedOn(os, "/opt/games/doom"))
		        << static_cast<int>(os);
	}
}

// A mount of a directory that contains a blocked one hands the guest the
// blocked one as a subdirectory, so ancestors of entries are blocked too
TEST(SystemPathTable, AncestorOfAnEntryIsBlocked)
{
	const std::vector<fs::path> row           = {"/", "/a/b/c"};
	const std::vector<fs::path> no_carve_outs = {};

	EXPECT_TRUE(MountPolicy::IsUnderSystemPathIn(
	        "/a", row, no_carve_outs, MountPolicy::Ancestors::Blocked));
	EXPECT_TRUE(MountPolicy::IsUnderSystemPathIn(
	        "/a/b", row, no_carve_outs, MountPolicy::Ancestors::Blocked));
	EXPECT_FALSE(MountPolicy::IsUnderSystemPathIn(
	        "/a/bc", row, no_carve_outs, MountPolicy::Ancestors::Blocked));
	EXPECT_FALSE(MountPolicy::IsUnderSystemPathIn(
	        "/a/b/d", row, no_carve_outs, MountPolicy::Ancestors::Blocked));
}

TEST(SystemPathTable, MacosPrivateAndPrivateVarAreBlockedAsAncestors)
{
	EXPECT_TRUE(BlockedOn(HostOs::MacOs, "/private"));
	EXPECT_TRUE(BlockedOn(HostOs::MacOs, "/private/var"));
	EXPECT_FALSE(BlockedOn(HostOs::MacOs, "/private/var/folders"));
	EXPECT_FALSE(BlockedOn(HostOs::MacOs, "/private/tmp"));
}

// The Windows list carries the user's temp directory under AppData, so
// blocking every parent of an entry would refuse the user's own profile
TEST(SystemPathTable, WindowsRowKeepsAncestorsOpen)
{
	EXPECT_EQ(MountPolicy::AncestorsFor(HostOs::Windows),
	          MountPolicy::Ancestors::Open);
	for (const auto os : kUnixRows) {
		EXPECT_EQ(MountPolicy::AncestorsFor(os), MountPolicy::Ancestors::Blocked)
		        << static_cast<int>(os);
	}

	const std::vector<fs::path> row           = {"/", "/a/b/c"};
	const std::vector<fs::path> no_carve_outs = {};
	EXPECT_FALSE(MountPolicy::IsUnderSystemPathIn(
	        "/a", row, no_carve_outs, MountPolicy::Ancestors::Open));
	EXPECT_TRUE(MountPolicy::IsUnderSystemPathIn(
	        "/a/b/c/d", row, no_carve_outs, MountPolicy::Ancestors::Open));
}

// A home directory that is a symlink into a blocked tree: /home -> /usr/home
// on older FreeBSD installs, /home -> /var/home on ostree Linux
TEST(SystemPathTable, SymlinkedHomeTreesAreCarvedOut)
{
	EXPECT_FALSE(BlockedOn(HostOs::FreeBsd, "/usr/home/user/games"));
	EXPECT_FALSE(BlockedOn(HostOs::DragonFly, "/usr/home/user/games"));
	EXPECT_FALSE(BlockedOn(HostOs::Linux, "/var/home/user/games"));
	EXPECT_TRUE(BlockedOn(HostOs::FreeBsd, "/usr/homes"));
	EXPECT_TRUE(BlockedOn(HostOs::Linux, "/var/lib"));
}

// A carve-out runs before the entries, so one that is a root, an entry or
// an ancestor of an entry would reopen that entry; every row is checked
TEST(SystemPathTable, CarveOutsLieStrictlyInsideAnEntryAndAboveNone)
{
	for (const auto os : kUnixRows) {
		const auto entries = MountPolicy::SystemPathsFor(os);
		for (const auto& carve_out : MountPolicy::CarveOutsFor(os)) {
			const auto carve_str = carve_out.string();
			EXPECT_FALSE(MountPolicy::IsBareRoot(carve_out)) << carve_str;

			bool inside_an_entry = false;
			for (const auto& entry : entries) {
				const auto entry_str = entry.string();
				EXPECT_FALSE(MountPolicy::PathIsAtOrBelow(entry_str,
				                                          carve_str))
				        << carve_str << " covers entry "
				        << entry_str;
				if (!MountPolicy::IsBareRoot(entry) &&
				    MountPolicy::PathIsAtOrBelow(carve_str,
				                                 entry_str)) {
					inside_an_entry = true;
				}
			}
			EXPECT_TRUE(inside_an_entry)
			        << carve_str << " is not inside any entry";
		}
	}
}

} // namespace
