// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "dos/drive_swap.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

namespace fs = std::filesystem;

// Temp-dir fixture, same shape as mount_policy_tests.cpp: random name
// plus creation check instead of mkdtemp for Windows portability.
class DriveSwapTest : public testing::Test {
protected:
	fs::path tmp_dir = {};

	static fs::path MakeTempDir()
	{
		std::random_device rd = {};
		auto dist = std::uniform_int_distribution<uint64_t>();
		for (int attempt = 0; attempt < 16; ++attempt) {
			const auto name = "drive_swap_" + std::to_string(dist(rd));
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

	fs::path CreateDir(const std::string& name)
	{
		const auto path = tmp_dir / name;
		fs::create_directories(path);
		return path;
	}

	// A 512-byte image with a FAT boot signature: passes image
	// validation, but fatDrive cannot parse it, which makes the
	// "Failed to mount image" branch reachable without a real
	// filesystem in the file.
	fs::path CreateFatStub(const std::string& name)
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
};

// -- ResolveImagePath --

TEST_F(DriveSwapTest, ResolveAbsolutePassesThrough)
{
	const auto abs      = tmp_dir / "nonexistent.img";
	const auto resolved = DriveSwap::ResolveImagePath(abs, tmp_dir, {});
	EXPECT_EQ(resolved, abs);
}

TEST_F(DriveSwapTest, ResolveRelativeUnderAnchor)
{
	const auto anchor = CreateDir("game");
	CreateFatStub("game/disk2.img");

	const auto resolved = DriveSwap::ResolveImagePath("disk2.img", anchor, {});
	EXPECT_EQ(resolved, anchor / "disk2.img");
}

TEST_F(DriveSwapTest, ResolveRelativeUnderRoot)
{
	const auto anchor = CreateDir("conf");
	const auto root   = CreateDir("images");
	CreateFatStub("images/disk2.img");

	const auto resolved = DriveSwap::ResolveImagePath("disk2.img", anchor, {root});
	EXPECT_EQ(resolved, root / "disk2.img");
}

TEST_F(DriveSwapTest, ResolveAnchorWinsOverRoot)
{
	const auto anchor = CreateDir("game");
	const auto root   = CreateDir("images");
	CreateFatStub("game/disk.img");
	CreateFatStub("images/disk.img");

	const auto resolved = DriveSwap::ResolveImagePath("disk.img", anchor, {root});
	EXPECT_EQ(resolved, anchor / "disk.img");
}

TEST_F(DriveSwapTest, ResolveFirstRootWins)
{
	const auto root_a = CreateDir("a");
	const auto root_b = CreateDir("b");
	CreateFatStub("a/disk.img");
	CreateFatStub("b/disk.img");

	const auto resolved = DriveSwap::ResolveImagePath("disk.img",
	                                                  {},
	                                                  {root_a, root_b});
	EXPECT_EQ(resolved, root_a / "disk.img");
}

TEST_F(DriveSwapTest, ResolveEmptyAnchorSkipped)
{
	const auto root = CreateDir("images");
	CreateFatStub("images/disk.img");

	const auto resolved = DriveSwap::ResolveImagePath("disk.img", {}, {root});
	EXPECT_EQ(resolved, root / "disk.img");
}

TEST_F(DriveSwapTest, ResolveMissPassesThrough)
{
	const auto root = CreateDir("images");

	const auto resolved = DriveSwap::ResolveImagePath("nowhere.img", {}, {root});
	EXPECT_EQ(resolved, fs::path("nowhere.img"));
}

// -- Swap --

TEST_F(DriveSwapTest, SwapRefusedWhenLocked)
{
	const auto root = CreateDir("images");
	const auto img  = CreateFatStub("images/disk.img");

	const auto result = DriveSwap::Swap('A', img, true, {}, {root});
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.error, "mount is locked");
}

TEST_F(DriveSwapTest, SwapRefusesNonAlphaDrive)
{
	const auto root = CreateDir("images");
	const auto img  = CreateFatStub("images/disk.img");

	const auto result = DriveSwap::Swap('1', img, false, {}, {root});
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.error, "Invalid drive letter");
}

TEST_F(DriveSwapTest, SwapDeniedWithoutRootsOrAnchor)
{
	const auto img = CreateFatStub("disk.img");

	const auto result = DriveSwap::Swap('A', img, false, {}, {});
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.error, "Blocked by mount policy");
}

TEST_F(DriveSwapTest, SwapDeniedOutsideRoots)
{
	const auto root    = CreateDir("images");
	const auto outside = CreateFatStub("elsewhere/disk.img");

	const auto result = DriveSwap::Swap('A', outside, false, {}, {root});
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.error, "Blocked by mount policy");
}

TEST_F(DriveSwapTest, SwapDeniedTraversalOutOfRoot)
{
	const auto root = CreateDir("images");
	CreateFatStub("outside.img");

	const auto result = DriveSwap::Swap(
	        'A', fs::path("..") / "outside.img", false, {}, {root});
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.error, "Blocked by mount policy");
}

// The stub passes policy validation and reaches fatDrive construction,
// which fails on it: proof the pipeline ran end to end. A swap that
// succeeds against a real FAT filesystem is covered by the integration
// tier (test_mount_policy.py), where mformat builds a genuine image.
TEST_F(DriveSwapTest, SwapAbsoluteStubReachesMount)
{
	const auto root = CreateDir("images");
	const auto img  = CreateFatStub("images/disk.img");

	const auto result = DriveSwap::Swap('A', img, false, {}, {root});
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.error, "Failed to mount image");
}

TEST_F(DriveSwapTest, SwapRelativeNameReachesMount)
{
	const auto root = CreateDir("images");
	CreateFatStub("images/disk2.img");

	const auto result = DriveSwap::Swap('A', "disk2.img", false, {}, {root});
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.error, "Failed to mount image");
}

TEST_F(DriveSwapTest, SwapLowercaseDriveAccepted)
{
	const auto root = CreateDir("images");
	CreateFatStub("images/disk.img");

	const auto result = DriveSwap::Swap('a', "disk.img", false, {}, {root});
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.error, "Failed to mount image");
}

} // namespace
