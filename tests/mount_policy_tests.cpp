// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "dos/programs/mount_policy.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include <gtest/gtest.h>

namespace {

namespace fs = std::filesystem;

// -- Test fixture: temp directory with automatic cleanup --

class MountPolicyTest : public testing::Test {
protected:
	fs::path tmp_dir = {};

	// mkdtemp does not exist on Windows; create_directory fails on an
	// existing path, so a random name plus creation check gives the
	// same no-clobber guarantee portably
	static fs::path MakeTempDir()
	{
		std::random_device rd = {};
		auto dist = std::uniform_int_distribution<uint64_t>();
		for (int attempt = 0; attempt < 16; ++attempt) {
			const auto name = "mount_policy_" + std::to_string(dist(rd));
			const auto candidate = fs::temp_directory_path() / name;
			std::error_code ec   = {};
			if (fs::create_directory(candidate, ec) && !ec) {
				fs::permissions(candidate, fs::perms::owner_all, ec);
				return candidate;
			}
		}
		return {};
	}

	void SetUp() override
	{
		tmp_dir = MakeTempDir();
		ASSERT_FALSE(tmp_dir.empty());
	}

	void TearDown() override
	{
		if (!tmp_dir.empty() && fs::exists(tmp_dir)) {
			fs::remove_all(tmp_dir);
		}
	}

	// Create a file with given content
	fs::path CreateFile(const std::string& name, const std::string& content = "")
	{
		const auto path = tmp_dir / name;
		fs::create_directories(path.parent_path());
		auto out = std::ofstream(path, std::ios::binary);
		out << content;
		return path;
	}

	// Create a directory
	fs::path CreateDir(const std::string& name)
	{
		const auto path = tmp_dir / name;
		fs::create_directories(path);
		return path;
	}

	// Create a symlink
	fs::path CreateSymlink(const fs::path& target, const std::string& link_name)
	{
		const auto link_path = tmp_dir / link_name;
		fs::create_symlink(target, link_path);
		return link_path;
	}

	// Write a minimal FAT boot sector image (512 bytes, 0x55AA at 510)
	fs::path CreateFatImage(const std::string& name)
	{
		const auto path = tmp_dir / name;
		fs::create_directories(path.parent_path());
		auto out = std::ofstream(path, std::ios::binary);
		auto buf = std::array<uint8_t, 512>{};
		buf[510] = 0x55;
		buf[511] = 0xAA;
		out.write(reinterpret_cast<const char*>(buf.data()), buf.size());
		return path;
	}

	// Write a minimal ISO 9660 image with "CD001" at offset 0x8001
	fs::path CreateIsoImage(const std::string& name)
	{
		const auto path = tmp_dir / name;
		fs::create_directories(path.parent_path());
		auto out = std::ofstream(path, std::ios::binary);
		// Write zeros up to 0x8001, then "CD001"
		auto zeroes = std::vector<char>(0x8001, '\0');
		out.write(zeroes.data(),
		          static_cast<std::streamsize>(zeroes.size()));
		out.write("CD001", 5);
		return path;
	}

	// Write a CUE sheet
	fs::path CreateCueSheet(const std::string& name)
	{
		return CreateFile(name,
		                  "FILE \"game.bin\" BINARY\n"
		                  "  TRACK 01 MODE1/2352\n"
		                  "    INDEX 01 00:00:00\n");
	}

	// Write a 1.44M floppy image (all zeroes, correct size)
	fs::path CreateFloppyImage(const std::string& name)
	{
		const auto path = tmp_dir / name;
		fs::create_directories(path.parent_path());
		auto out = std::ofstream(path, std::ios::binary);
		auto buf = std::vector<char>(1474560, '\0');
		out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
		return path;
	}
};

// -- CanonicalizeExisting --

TEST_F(MountPolicyTest, CanonicalizeExistingDir)
{
	const auto dir    = CreateDir("gamedir");
	const auto result = MountPolicy::CanonicalizeExisting(dir);
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(*result, fs::canonical(dir));
}

TEST_F(MountPolicyTest, CanonicalizeNonexistent)
{
	const auto result = MountPolicy::CanonicalizeExisting(tmp_dir /
	                                                      "does_not_exist");
	EXPECT_FALSE(result.has_value());
}

TEST_F(MountPolicyTest, CanonicalizeResolvesTraversal)
{
	const auto dir       = CreateDir("a/b");
	const auto traversal = dir / ".." / "b";
	const auto result    = MountPolicy::CanonicalizeExisting(traversal);
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(*result, fs::canonical(dir));
}

// -- HasSymlinkComponent --

TEST_F(MountPolicyTest, NoSymlinksInPath)
{
	const auto dir = CreateDir("clean/sub");
	EXPECT_FALSE(MountPolicy::HasSymlinkComponent(dir));
}

TEST_F(MountPolicyTest, SymlinkInPath)
{
	const auto real_dir = CreateDir("real_target");
	const auto link     = CreateSymlink(real_dir, "link_to_target");
	EXPECT_TRUE(MountPolicy::HasSymlinkComponent(link));
}

TEST_F(MountPolicyTest, SymlinkAsIntermediateComponent)
{
	const auto real_dir = CreateDir("real_parent/child");
	const auto link = CreateSymlink(real_dir.parent_path(), "link_parent");
	const auto through_link = tmp_dir / "link_parent" / "child";
	EXPECT_TRUE(MountPolicy::HasSymlinkComponent(through_link));
}

// -- IsUnderSystemPath --

#if !defined(WIN32)
TEST(MountPolicySystemPath, RootIsSystemPath)
{
	EXPECT_TRUE(MountPolicy::IsUnderSystemPath(fs::path("/")));
}

TEST(MountPolicySystemPath, EtcIsSystemPath)
{
	EXPECT_TRUE(MountPolicy::IsUnderSystemPath(fs::path("/etc")));
}

TEST(MountPolicySystemPath, EtcShadowIsSystemPath)
{
	EXPECT_TRUE(MountPolicy::IsUnderSystemPath(fs::path("/etc/shadow")));
}

TEST(MountPolicySystemPath, ProcIsSystemPath)
{
	EXPECT_TRUE(MountPolicy::IsUnderSystemPath(fs::path("/proc")));
}

TEST(MountPolicySystemPath, EtceteraIsNotSystemPath)
{
	// "/etcetera" must NOT match "/etc"
	EXPECT_FALSE(MountPolicy::IsUnderSystemPath(fs::path("/etcetera")));
}

TEST(MountPolicySystemPath, HomeIsNotSystemPath)
{
	EXPECT_FALSE(MountPolicy::IsUnderSystemPath(fs::path("/home/user")));
}

TEST(MountPolicySystemPath, OptIsNotSystemPath)
{
	EXPECT_FALSE(MountPolicy::IsUnderSystemPath(fs::path("/opt/games")));
}
#endif

// -- IsUnderAnyRoot --

TEST_F(MountPolicyTest, PathUnderRoot)
{
	const auto root = CreateDir("allowed");
	const auto sub  = CreateDir("allowed/game1");
	EXPECT_TRUE(MountPolicy::IsUnderAnyRoot(fs::canonical(sub), {root}));
}

TEST_F(MountPolicyTest, PathIsRoot)
{
	const auto root = CreateDir("allowed");
	EXPECT_TRUE(MountPolicy::IsUnderAnyRoot(fs::canonical(root), {root}));
}

TEST_F(MountPolicyTest, PathOutsideRoot)
{
	const auto root    = CreateDir("allowed");
	const auto outside = CreateDir("forbidden");
	EXPECT_FALSE(MountPolicy::IsUnderAnyRoot(fs::canonical(outside), {root}));
}

TEST_F(MountPolicyTest, PathUnderSecondRoot)
{
	const auto root1 = CreateDir("root1");
	const auto root2 = CreateDir("root2");
	const auto sub   = CreateDir("root2/game");
	EXPECT_TRUE(MountPolicy::IsUnderAnyRoot(fs::canonical(sub), {root1, root2}));
}

TEST_F(MountPolicyTest, EmptyRootsRejectsEverything)
{
	const auto dir = CreateDir("anything");
	EXPECT_FALSE(MountPolicy::IsUnderAnyRoot(fs::canonical(dir), {}));
}

// -- ValidateDiskImageStructure --

TEST_F(MountPolicyTest, FatImagePasses)
{
	const auto img = CreateFatImage("test.img");
	EXPECT_TRUE(MountPolicy::ValidateDiskImageStructure(img));
}

TEST_F(MountPolicyTest, IsoImagePasses)
{
	const auto img = CreateIsoImage("test.iso");
	EXPECT_TRUE(MountPolicy::ValidateDiskImageStructure(img));
}

TEST_F(MountPolicyTest, CueSheetPasses)
{
	const auto cue = CreateCueSheet("test.cue");
	EXPECT_TRUE(MountPolicy::ValidateDiskImageStructure(cue));
}

TEST_F(MountPolicyTest, FloppyImagePasses)
{
	const auto img = CreateFloppyImage("floppy.img");
	EXPECT_TRUE(MountPolicy::ValidateDiskImageStructure(img));
}

TEST_F(MountPolicyTest, RandomFileRejectsAsImage)
{
	const auto junk = CreateFile("junk.dat", "this is not a disk image");
	EXPECT_FALSE(MountPolicy::ValidateDiskImageStructure(junk));
}

TEST_F(MountPolicyTest, EmptyFileRejectsAsImage)
{
	const auto empty = CreateFile("empty.img");
	EXPECT_FALSE(MountPolicy::ValidateDiskImageStructure(empty));
}

TEST_F(MountPolicyTest, CueWithoutFileLine)
{
	const auto bad_cue = CreateFile("bad.cue",
	                                "REM this is not a valid cue\n"
	                                "TRACK 01 AUDIO\n");
	EXPECT_FALSE(MountPolicy::ValidateDiskImageStructure(bad_cue));
}

// -- ValidateDirectoryMount --

TEST_F(MountPolicyTest, DirMountUnderConfAnchor)
{
	const auto anchor   = CreateDir("game_conf");
	const auto game_dir = CreateDir("game_conf/data");

	const auto verdict = MountPolicy::ValidateDirectoryMount(
	        game_dir, anchor, {}, DirMountPolicy::WhitelistEnforced);

	EXPECT_TRUE(verdict.allowed);
	EXPECT_EQ(verdict.reason, DenyReason::None);
}

TEST_F(MountPolicyTest, DirMountOutsideWhitelist)
{
	const auto anchor  = CreateDir("game_conf");
	const auto outside = CreateDir("somewhere_else");

	const auto verdict = MountPolicy::ValidateDirectoryMount(
	        outside, anchor, {}, DirMountPolicy::WhitelistEnforced);

	EXPECT_FALSE(verdict.allowed);
	EXPECT_EQ(verdict.reason, DenyReason::OutsideWhitelist);
}

TEST_F(MountPolicyTest, DirMountUnderAllowedBase)
{
	const auto anchor   = CreateDir("game_conf");
	const auto base     = CreateDir("games_root");
	const auto game_dir = CreateDir("games_root/doom");

	const auto verdict = MountPolicy::ValidateDirectoryMount(
	        game_dir, anchor, {base}, DirMountPolicy::WhitelistEnforced);

	EXPECT_TRUE(verdict.allowed);
	EXPECT_EQ(verdict.reason, DenyReason::None);
}

TEST_F(MountPolicyTest, DirMountOwnerTrustedSkipsWhitelist)
{
	const auto anchor   = CreateDir("game_conf");
	const auto anywhere = CreateDir("random_place");

	const auto verdict = MountPolicy::ValidateDirectoryMount(
	        anywhere, anchor, {}, DirMountPolicy::OwnerTrusted);

	EXPECT_TRUE(verdict.allowed);
}

#if !defined(WIN32)
TEST_F(MountPolicyTest, DirMountSystemPathRejectedEvenOwnerTrusted)
{
	// /etc exists on the system, not in our temp dir
	const auto verdict = MountPolicy::ValidateDirectoryMount(
	        fs::path("/etc"), fs::path("/etc"), {}, DirMountPolicy::OwnerTrusted);

	EXPECT_FALSE(verdict.allowed);
	EXPECT_EQ(verdict.reason, DenyReason::SystemPath);
}
#endif

TEST_F(MountPolicyTest, DirMountNonexistentPath)
{
	const auto verdict = MountPolicy::ValidateDirectoryMount(
	        tmp_dir / "nope", tmp_dir, {}, DirMountPolicy::WhitelistEnforced);

	EXPECT_FALSE(verdict.allowed);
	EXPECT_EQ(verdict.reason, DenyReason::DoesNotResolve);
}

TEST_F(MountPolicyTest, DirMountThroughSymlink)
{
	const auto real = CreateDir("real_game");
	const auto link = CreateSymlink(real, "link_game");

	const auto verdict = MountPolicy::ValidateDirectoryMount(
	        link, tmp_dir, {}, DirMountPolicy::WhitelistEnforced);

	EXPECT_FALSE(verdict.allowed);
	EXPECT_EQ(verdict.reason, DenyReason::SymlinkComponent);
}

TEST_F(MountPolicyTest, DirMountTraversalOutOfAnchor)
{
	const auto anchor    = CreateDir("anchor");
	const auto outside   = CreateDir("outside");
	const auto traversal = anchor / ".." / "outside";

	const auto verdict = MountPolicy::ValidateDirectoryMount(
	        traversal, anchor, {}, DirMountPolicy::WhitelistEnforced);

	EXPECT_FALSE(verdict.allowed);
	EXPECT_EQ(verdict.reason, DenyReason::OutsideWhitelist);
}

TEST_F(MountPolicyTest, BundledDrivesRootAllowedAsBase)
{
	// Simulates the InitPolicyConfig fix: the bundled drives/ root is
	// added to allowed_bases, so drives/y (or any letter) passes the
	// whitelist, while a directory outside it is still denied.
	const auto anchor   = CreateDir("conf");
	const auto drives   = CreateDir("drives");
	const auto drives_y = CreateDir("drives/y");
	const auto outside  = CreateDir("hostile");

	const std::vector<fs::path> bases = {fs::canonical(drives)};

	const auto allowed = MountPolicy::ValidateDirectoryMount(
	        drives_y, anchor, bases, DirMountPolicy::WhitelistEnforced);
	EXPECT_TRUE(allowed.allowed);
	EXPECT_EQ(allowed.reason, DenyReason::None);

	const auto denied = MountPolicy::ValidateDirectoryMount(
	        outside, anchor, bases, DirMountPolicy::WhitelistEnforced);
	EXPECT_FALSE(denied.allowed);
	EXPECT_EQ(denied.reason, DenyReason::OutsideWhitelist);
}

#if !defined(WIN32)
TEST_F(MountPolicyTest, BundledDrivesBaseDoesNotOpenSystemPaths)
{
	// Even with drives/ in allowed_bases, /etc is still blocked by
	// IsUnderSystemPath which runs before the whitelist check.
	const auto anchor = CreateDir("conf");
	const auto drives = CreateDir("drives");

	const std::vector<fs::path> bases = {fs::canonical(drives)};

	const auto verdict = MountPolicy::ValidateDirectoryMount(
	        fs::path("/etc"), anchor, bases, DirMountPolicy::WhitelistEnforced);
	EXPECT_FALSE(verdict.allowed);
	EXPECT_EQ(verdict.reason, DenyReason::SystemPath);
}
#endif

// -- ValidateImagePath --

TEST_F(MountPolicyTest, ImagePathValidFat)
{
	const auto img = CreateFatImage("disk.img");

	const auto verdict = MountPolicy::ValidateImagePath(img,
	                                                    MountOrigin::Interactive,
	                                                    {});

	EXPECT_TRUE(verdict.allowed);
	EXPECT_EQ(verdict.reason, DenyReason::None);
}

TEST_F(MountPolicyTest, ImagePathValidIso)
{
	const auto img = CreateIsoImage("game.iso");

	const auto verdict = MountPolicy::ValidateImagePath(img,
	                                                    MountOrigin::Interactive,
	                                                    {});

	EXPECT_TRUE(verdict.allowed);
}

TEST_F(MountPolicyTest, ImagePathJunkFile)
{
	const auto junk = CreateFile("notadisk.dat", "definitely not a disk image");

	const auto verdict = MountPolicy::ValidateImagePath(junk,
	                                                    MountOrigin::Interactive,
	                                                    {});

	EXPECT_FALSE(verdict.allowed);
	EXPECT_EQ(verdict.reason, DenyReason::NotADiskImage);
}

TEST_F(MountPolicyTest, ImagePathNonexistent)
{
	const auto verdict = MountPolicy::ValidateImagePath(tmp_dir / "missing.iso",
	                                                    MountOrigin::Interactive,
	                                                    {});

	EXPECT_FALSE(verdict.allowed);
	EXPECT_EQ(verdict.reason, DenyReason::DoesNotResolve);
}

TEST_F(MountPolicyTest, ImagePathDirectory)
{
	const auto dir = CreateDir("not_a_file");

	const auto verdict = MountPolicy::ValidateImagePath(dir,
	                                                    MountOrigin::Interactive,
	                                                    {});

	EXPECT_FALSE(verdict.allowed);
	EXPECT_EQ(verdict.reason, DenyReason::NotRegularFile);
}

TEST_F(MountPolicyTest, ImagePathSymlink)
{
	const auto real = CreateFatImage("real.img");
	const auto link = CreateSymlink(real, "link.img");

	const auto verdict = MountPolicy::ValidateImagePath(link,
	                                                    MountOrigin::Interactive,
	                                                    {});

	EXPECT_FALSE(verdict.allowed);
	EXPECT_EQ(verdict.reason, DenyReason::SymlinkComponent);
}

#if !defined(WIN32)
TEST_F(MountPolicyTest, ImagePathUnderSystemDir)
{
	// /etc/hostname is a regular file on most Linux systems
	if (!fs::exists("/etc/hostname")) {
		GTEST_SKIP() << "/etc/hostname not available";
	}
	const auto verdict = MountPolicy::ValidateImagePath(
	        fs::path("/etc/hostname"), MountOrigin::Interactive, {});

	EXPECT_FALSE(verdict.allowed);
	EXPECT_EQ(verdict.reason, DenyReason::SystemPath);
}
#endif

TEST_F(MountPolicyTest, ImagePathApiOutsideAllowedRoots)
{
	const auto allowed   = CreateDir("allowed_images");
	const auto elsewhere = CreateFatImage("elsewhere/disk.img");

	const auto verdict = MountPolicy::ValidateImagePath(elsewhere,
	                                                    MountOrigin::Api,
	                                                    {allowed});

	EXPECT_FALSE(verdict.allowed);
	EXPECT_EQ(verdict.reason, DenyReason::OutsideWhitelist);
}

TEST_F(MountPolicyTest, ImagePathApiUnderAllowedRoot)
{
	const auto allowed = CreateDir("allowed_images");
	const auto img     = CreateFatImage("allowed_images/disk.img");

	const auto verdict = MountPolicy::ValidateImagePath(img,
	                                                    MountOrigin::Api,
	                                                    {allowed});

	EXPECT_TRUE(verdict.allowed);
}

TEST_F(MountPolicyTest, ImagePathInteractiveIgnoresRoots)
{
	// Interactive origin does not check allowed_image_roots
	const auto allowed   = CreateDir("allowed_images");
	const auto elsewhere = CreateFatImage("elsewhere/disk.img");

	const auto verdict = MountPolicy::ValidateImagePath(elsewhere,
	                                                    MountOrigin::Interactive,
	                                                    {allowed});

	EXPECT_TRUE(verdict.allowed);
}

// -- Attack scenarios --

TEST_F(MountPolicyTest, HardlinkToPasswdCaughtByStructuralValidation)
{
	// A hardlink to /etc/passwd would pass symlink checks but fail
	// structural validation because /etc/passwd is not a disk image.
	// We simulate this with a non-image regular file.
	const auto fake = CreateFile("hardlink_target.img",
	                             "root:x:0:0:root:/root:/bin/bash\n");

	const auto verdict = MountPolicy::ValidateImagePath(fake,
	                                                    MountOrigin::Interactive,
	                                                    {});

	EXPECT_FALSE(verdict.allowed);
	EXPECT_EQ(verdict.reason, DenyReason::NotADiskImage);
}

TEST_F(MountPolicyTest, SymlinkToEtcShadowViaDirectory)
{
	// Attacker creates a symlink disguised as a game directory
	// pointing to /etc
	if (!fs::exists("/etc")) {
		GTEST_SKIP() << "/etc not available";
	}
	const auto link = CreateSymlink(fs::path("/etc"), "innocent_game");

	const auto verdict = MountPolicy::ValidateDirectoryMount(
	        link, tmp_dir, {}, DirMountPolicy::WhitelistEnforced);

	EXPECT_FALSE(verdict.allowed);
	EXPECT_EQ(verdict.reason, DenyReason::SymlinkComponent);
}

TEST_F(MountPolicyTest, TraversalEscapeFromConfAnchor)
{
	// Malicious conf tries to mount ../../etc from within the
	// anchor directory
	const auto anchor         = CreateDir("confined");
	const auto malicious_path = anchor / ".." / ".." / "etc";

	const auto verdict = MountPolicy::ValidateDirectoryMount(
	        malicious_path, anchor, {}, DirMountPolicy::WhitelistEnforced);

	// Either DoesNotResolve (if /etc doesn't canonicalize to
	// something under our roots) or SystemPath or OutsideWhitelist.
	// Any of these is correct rejection.
	EXPECT_FALSE(verdict.allowed);
}

// -- Lock --

TEST(MountPolicyLock, InitiallyUnlocked)
{
	// Lock is a global one-way latch. If a prior test already locked
	// it, this test is not meaningful - but we cannot unlatch.
	// In a fresh run it starts unlocked.
	if (MountPolicy::IsLocked()) {
		GTEST_SKIP() << "Lock already set by a prior test";
	}
	EXPECT_FALSE(MountPolicy::IsLocked());
}

TEST(MountPolicyLock, LockIsOneWay)
{
	MountPolicy::Lock();
	EXPECT_TRUE(MountPolicy::IsLocked());

	// Locking again doesn't crash or toggle
	MountPolicy::Lock();
	EXPECT_TRUE(MountPolicy::IsLocked());
}

// -- Config parsing --

TEST_F(MountPolicyTest, ParseValidConfig)
{
	const auto dir1 = CreateDir("games");
	const auto dir2 = CreateDir("images");

	const auto cfg = CreateFile("test.conf",
	                            "[webserver]\n"
	                            "mount_allowed_bases = " +
	                                    dir1.string() + ";" + dir2.string() +
	                                    "\n"
	                                    "mount_allowed_image_roots = " +
	                                    dir1.string() + "\n");

	const auto paths = MountPolicy::ParsePolicyConfig(cfg);
	EXPECT_EQ(paths.allowed_bases.size(), 2u);
	EXPECT_EQ(paths.allowed_image_roots.size(), 1u);
}

TEST_F(MountPolicyTest, ParseIgnoresOtherSections)
{
	const auto dir = CreateDir("games");

	const auto cfg = CreateFile("test.conf",
	                            "[dosbox]\n"
	                            "mount_allowed_bases = " +
	                                    dir.string() + "\n");

	const auto paths = MountPolicy::ParsePolicyConfig(cfg);
	EXPECT_TRUE(paths.allowed_bases.empty());
}

TEST_F(MountPolicyTest, ParseCapAt3)
{
	const auto d1 = CreateDir("a");
	const auto d2 = CreateDir("b");
	const auto d3 = CreateDir("c");
	const auto d4 = CreateDir("d");
	const auto d5 = CreateDir("e");

	const auto cfg = CreateFile("test.conf",
	                            "[webserver]\n"
	                            "mount_allowed_bases = " +
	                                    d1.string() + ";" + d2.string() +
	                                    ";" + d3.string() + ";" + d4.string() +
	                                    ";" + d5.string() + "\n");

	const auto paths = MountPolicy::ParsePolicyConfig(cfg);
	EXPECT_EQ(paths.allowed_bases.size(), 3u);
}

TEST_F(MountPolicyTest, ParseNonexistentPathDropped)
{
	const auto cfg = CreateFile("test.conf",
	                            "[webserver]\n"
	                            "mount_allowed_bases = "
	                            "/nonexistent/path/xyz123\n");

	const auto paths = MountPolicy::ParsePolicyConfig(cfg);
	EXPECT_TRUE(paths.allowed_bases.empty());
}

TEST_F(MountPolicyTest, ParseEmptyFileGivesEmpty)
{
	const auto cfg = CreateFile("empty.conf");

	const auto paths = MountPolicy::ParsePolicyConfig(cfg);
	EXPECT_TRUE(paths.allowed_bases.empty());
	EXPECT_TRUE(paths.allowed_image_roots.empty());
}

TEST_F(MountPolicyTest, ParseMissingFileGivesEmpty)
{
	const auto paths = MountPolicy::ParsePolicyConfig(tmp_dir /
	                                                  "does_not_exist.conf");
	EXPECT_TRUE(paths.allowed_bases.empty());
	EXPECT_TRUE(paths.allowed_image_roots.empty());
}

TEST_F(MountPolicyTest, ParseIgnoresComments)
{
	const auto dir = CreateDir("games");

	const auto cfg = CreateFile("test.conf",
	                            "[webserver]\n"
	                            "# mount_allowed_bases = " +
	                                    dir.string() + "\n");

	const auto paths = MountPolicy::ParsePolicyConfig(cfg);
	EXPECT_TRUE(paths.allowed_bases.empty());
}

TEST_F(MountPolicyTest, ParseEnvExpansion)
{
	// $HOME is always set on Linux
	const auto home = std::string(std::getenv("HOME"));
	if (home.empty()) {
		GTEST_SKIP() << "HOME not set";
	}

	// Use a path that actually exists under HOME
	const auto cfg = CreateFile("test.conf",
	                            "[webserver]\n"
	                            "mount_allowed_bases = $HOME\n");

	const auto paths = MountPolicy::ParsePolicyConfig(cfg);
	ASSERT_EQ(paths.allowed_bases.size(), 1u);
	EXPECT_EQ(paths.allowed_bases[0], fs::canonical(home));
}

// -- Review fixes: additional coverage --

TEST_F(MountPolicyTest, ApiEmptyRootsDeniesAll)
{
	const auto img = CreateFatImage("any_image.img");

	const auto verdict = MountPolicy::ValidateImagePath(img, MountOrigin::Api, {});

	EXPECT_FALSE(verdict.allowed);
	EXPECT_EQ(verdict.reason, DenyReason::OutsideWhitelist);
}

TEST_F(MountPolicyTest, CueCommentNotMatchedAsFile)
{
	// REM line containing FILE should not trigger a false positive
	const auto bad_cue = CreateFile("comment.cue",
	                                "REM this FILE is just a comment\n"
	                                "TRACK 01 AUDIO\n");
	EXPECT_FALSE(MountPolicy::ValidateDiskImageStructure(bad_cue));
}

TEST_F(MountPolicyTest, CueWithIndentedFileDirective)
{
	const auto cue = CreateFile("indented.cue",
	                            "  FILE \"game.bin\" BINARY\n"
	                            "    TRACK 01 MODE1/2352\n"
	                            "      INDEX 01 00:00:00\n");
	EXPECT_TRUE(MountPolicy::ValidateDiskImageStructure(cue));
}

// -- Windows-specific tests --

#if defined(WIN32)

TEST(MountPolicyWindows, CaseInsensitiveSystemPathMatch)
{
	// On Windows, C:\Windows and c:\windows must both be blocked.
	// This test uses the actual system paths, so it only runs on Windows.
	const auto lower = std::filesystem::path("c:\\windows\\system32");
	const auto upper = std::filesystem::path("C:\\Windows\\System32");

	// Both casings should be recognized as system paths (assuming
	// the system drive has a Windows directory).
	if (std::filesystem::exists(lower)) {
		EXPECT_TRUE(MountPolicy::IsUnderSystemPath(lower));
		EXPECT_TRUE(MountPolicy::IsUnderSystemPath(upper));
	} else {
		GTEST_SKIP() << "C:\\Windows not found on this system";
	}
}

TEST(MountPolicyWindows, CaseInsensitiveRootMatch)
{
	const auto root = std::filesystem::path("C:\\AllowedRoot");
	const auto file_match = std::filesystem::path("c:\\allowedroot\\game.img");
	const auto file_nomatch = std::filesystem::path("C:\\OtherRoot\\game.img");

	const std::vector<std::filesystem::path> roots = {root};

	EXPECT_TRUE(MountPolicy::IsUnderAnyRoot(file_match, roots));
	EXPECT_FALSE(MountPolicy::IsUnderAnyRoot(file_nomatch, roots));
}

TEST(MountPolicyWindows, DeviceNamespacePathRejected)
{
	// Device namespace paths must never reach the filesystem layer.
	EXPECT_TRUE(MountPolicy::IsDeviceNamespacePath("\\\\.\\PhysicalDrive0"));
	EXPECT_TRUE(MountPolicy::IsDeviceNamespacePath("\\\\?\\C:\\file.img"));
	EXPECT_FALSE(MountPolicy::IsDeviceNamespacePath("C:\\normal\\path"));
	EXPECT_FALSE(MountPolicy::IsDeviceNamespacePath("\\\\server\\share"));
}

#endif // WIN32

} // namespace
