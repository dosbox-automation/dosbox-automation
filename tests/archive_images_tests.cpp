// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/programs/archive_images.h"

#include "archive_fixtures.h"
#include "config/setup.h"
#include "dos/archive/archive_helper_client.h"
#include "dos/archive/archive_reader.h"
#include "dos/archive/sidecar_record.h"
#include "dos/cdrom.h"
#include "dos/drive_swap.h"
#include "dos/programs/mount.h"
#include "dos/programs/mount_policy.h"
#include "dosbox_test_fixture.h"
#include "ints/bios_disk.h"
#include "misc/cross.h"
#include "test_temp_dir.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using ArchiveFixtures::FileEntry;
using ArchiveFixtures::Filter;
using ArchiveFixtures::IszChunk;
using ArchiveFixtures::Kind;
using ArchiveFixtures::MinimalIso;
using ArchiveFixtures::MtimeOf;
using ArchiveFixtures::WriteArchive;
using ArchiveFixtures::WriteCompressed;
using ArchiveFixtures::WriteIsz;

namespace {

namespace std_fs = std::filesystem;

// A 320K DOS 1.x floppy: fatDrive synthesizes the BPB from the media byte.
std::string FloppyBytes()
{
	std::string img(327680, '\0');
	img[512] = '\xFF';
	return img;
}

std::string Slurp(const std_fs::path& path)
{
	std::ifstream in(path, std::ios::binary);
	std::stringstream s;
	s << in.rdbuf();
	return s.str();
}

class ArchiveImagesTest : public DOSBoxTestFixture {
protected:
	void SetUp() override
	{
		DOSBoxTestFixture::SetUp();
		MountPolicy::InitPolicyConfig({});
		tmp = TestTempDir::MakeUnique("archive_images_");
		ASSERT_FALSE(tmp.empty());
		ArchiveMount::SetHelperPathOverride(DOSBOX_ARCHIVE_HELPER_PATH);
		Set("archive_cache_path", (tmp / "cache").string());
		Set("archive_sidecar_path", (tmp / "saves").string());
		MOUNT::ResetArchiveCacheForTests();
		auto error = std::string();
		ASSERT_TRUE(WriteArchive(
		        tmp / "game.zip",
		        Kind::ZipDeflate,
		        {
		                {"disks/", "", true},
		                {"disks/disk1.img", FloppyBytes(), false, false, 700000000},
		                {"disks/disk2.img", FloppyBytes(), false, false, 700000002},
		                {"disks/diskette-three.img", FloppyBytes(), false, false, 700000003},
		                {"cd/", "", true},
		                {"cd/game.iso", MinimalIso()},
		                {"cd/track.bin", MinimalIso()},
		                {"cd/game.cue",
		                 "FILE \"track.bin\" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n"},
		                {"cd/evil.cue", "file \"/etc/passwd\" BINARY\n"},
		                {"cd/game.mds", "MEDIA DESCRIPTOR"}
                },
		        nullptr,
		        &error))
		        << error;
	}
	void TearDown() override
	{
		imageDiskList.fill(nullptr);
		diskSwap.fill(nullptr);
		ArchiveImages::ReleaseAllForTests();
		ArchiveMount::SetHelperPathOverride({});
		MOUNT::ResetArchiveCacheForTests();
		std::error_code ec = {};
		std_fs::remove_all(tmp, ec);
		DOSBoxTestFixture::TearDown();
	}
	static void Set(const char* name, const std::string& value)
	{
		auto section = get_section("dos");
		ASSERT_NE(section, nullptr);
		auto property = section->GetProperty(name);
		ASSERT_NE(property, nullptr) << name;
		ASSERT_TRUE(property->SetValue(value)) << name << " = " << value;
	}
	ArchiveImages::Result Resolve(const std::string& member,
	                              const bool read_only = false)
	{
		return ArchiveImages::Resolve(RequestFor(member, read_only));
	}
	// What MOUNT sends for a drive's first image, or the swap for a
	// later one (drive_index set, nothing known about the drive's mode).
	ArchiveImages::Request RequestFor(const std::string& member,
	                                  const bool read_only        = false,
	                                  const std_fs::path& archive = {})
	{
		ArchiveImages::Request request = {};
		request.member.archive = archive.empty() ? tmp / "game.zip" : archive;
		request.member.member       = member;
		request.read_only           = read_only;
		request.allowed_image_roots = MountPolicy::AllowedImageRoots();
		request.conf_anchor         = MountPolicy::ConfAnchor();
		return request;
	}
	ArchiveImages::Result SwapInto(const int drive, const std::string& member,
	                               const std_fs::path& archive = {})
	{
		auto request        = RequestFor(member, false, archive);
		request.drive_index = drive;
		return ArchiveImages::Resolve(request);
	}
	// MOUNT keeps the first image's hold and records the drive's mode.
	void MountOnDrive(const int drive, ArchiveImages::Result& first,
	                  const bool read_only, const std::string& sidecar = {})
	{
		ASSERT_TRUE(first.resolved) << first.detail;
		ArchiveImages::Keep(drive, {first.resolved->hold});
		ArchiveImages::DriveMode mode = {};
		mode.archive      = std_fs::canonical(tmp / "game.zip");
		mode.read_only    = read_only;
		mode.sidecar_path = sidecar;
		ArchiveImages::SetDriveMode(drive, mode);
	}
	// Plan 4b: the container itself is the member path.
	ArchiveImages::Result ResolveLone(const std_fs::path& container,
	                                  const bool read_only = false)
	{
		const auto lone = ArchiveMount::LoneImageMember(container);
		EXPECT_TRUE(lone) << container;
		ArchiveImages::Request request = {};
		request.member    = lone.value_or(ArchiveMount::MemberPath{});
		request.read_only = read_only;
		request.allowed_image_roots = MountPolicy::AllowedImageRoots();
		request.conf_anchor         = MountPolicy::ConfAnchor();
		return ArchiveImages::Resolve(request);
	}
	std_fs::path tmp = {};
};

TEST_F(ArchiveImagesTest, ALoneMemberIsTheOnlyFileOfItsContainer)
{
	ASSERT_TRUE(WriteArchive(tmp / "game.imz",
	                         Kind::ZipDeflate,
	                         {
	                                 {"DISK1.IMA", FloppyBytes()}
        }));
	ASSERT_TRUE(WriteCompressed(tmp / "disk2.img.xz", Filter::Xz, FloppyBytes()));
	for (const auto* name : {"game.imz", "disk2.img.xz"}) {
		const auto result = ResolveLone(tmp / name);
		ASSERT_TRUE(result.resolved) << name << ": " << result.detail;
		const auto& r = *result.resolved;
		EXPECT_EQ(r.host_file.parent_path().filename(), ".images") << name;
		EXPECT_EQ(Slurp(r.host_file), FloppyBytes()) << name;
		// A zip member keeps its own date; a raw image takes the
		// container's (plan 4b, D13.3).
		const auto expected = std::string(name).ends_with(".imz")
		                            ? int64_t{631152000}
		                            : MtimeOf(tmp / name);
		EXPECT_EQ(MtimeOf(r.host_file), expected) << name;
	}
}

TEST_F(ArchiveImagesTest, AnIszIsServedFromTheCacheAsAnIso)
{
	const auto iso = MinimalIso() + std::string(64 * 1024, 'z');
	ASSERT_TRUE(WriteIsz(tmp / "game.isz", iso, IszChunk::Zlib));
	const auto result = ResolveLone(tmp / "game.isz");
	ASSERT_TRUE(result.resolved) << result.detail;
	const auto& r = *result.resolved;
	EXPECT_EQ(r.member_name, "game.iso");
	EXPECT_EQ(Slurp(r.host_file), iso);
	EXPECT_NE(r.host_file.string().find((tmp / "cache").string()),
	          std::string::npos);
	EXPECT_FALSE(std_fs::exists(tmp / "saves"));
}

TEST_F(ArchiveImagesTest, AContainerWithTwoFilesHasNoLoneMember)
{
	ASSERT_TRUE(WriteArchive(tmp / "two.imz",
	                         Kind::ZipDeflate,
	                         {
	                                 {"DISK1.IMA", FloppyBytes()},
	                                 {"DISK2.IMA", FloppyBytes()}
        }));
	const auto result = ResolveLone(tmp / "two.imz");
	EXPECT_FALSE(result.resolved);
	EXPECT_EQ(result.verdict.reason, DenyReason::ArchiveMemberNotFound);
	EXPECT_NE(result.detail.find("2 files"), std::string::npos) << result.detail;
}

TEST_F(ArchiveImagesTest, KindIsDecidedByTheExtension)
{
	using ArchiveImages::Kind;
	EXPECT_EQ(ArchiveImages::KindOf("cd/GAME.ISO"), Kind::CdImage);
	EXPECT_EQ(ArchiveImages::KindOf("a.cue"), Kind::CdImage);
	EXPECT_EQ(ArchiveImages::KindOf("a.bin"), Kind::CdImage);
	EXPECT_EQ(ArchiveImages::KindOf("a.mds"), Kind::CdCompanion);
	EXPECT_EQ(ArchiveImages::KindOf("a.CCD"), Kind::CdCompanion);
	EXPECT_EQ(ArchiveImages::KindOf("disk1.img"), Kind::DiskImage);
	EXPECT_EQ(ArchiveImages::KindOf("noext"), Kind::DiskImage);
}

TEST_F(ArchiveImagesTest, AnIsoIsServedFromTheCache)
{
	const auto result = Resolve("cd/game.iso");
	ASSERT_TRUE(result.resolved) << result.detail;
	const auto& r = *result.resolved;
	EXPECT_EQ(Slurp(r.host_file), MinimalIso());
	EXPECT_EQ(r.member_name, "cd/game.iso");
	EXPECT_NE(r.host_file.string().find((tmp / "cache").string()),
	          std::string::npos);
	ASSERT_TRUE(r.hold && r.hold->lease);
	EXPECT_FALSE(std_fs::exists(tmp / "saves"));
}

TEST_F(ArchiveImagesTest, ACueIsRewrittenToTheCachedTrack)
{
	const auto result = Resolve("cd/game.cue");
	ASSERT_TRUE(result.resolved) << result.detail;
	const auto& r = *result.resolved;
	EXPECT_EQ(r.host_file.extension(), ".cue");
	// The sheet sits beside its tracks: the loader takes nothing else
	// (ada-it53).
	EXPECT_EQ(r.host_file.parent_path(), r.hold->lease->FilesDirectory());
	const auto text = Slurp(r.host_file);
	EXPECT_EQ(text.find("track.bin"), std::string::npos) << text;
	const auto first = text.find('"') + 1;
	const auto name  = text.substr(first, text.find('"', first) - first);
	EXPECT_EQ(name.find_first_of("/\\:"), std::string::npos) << name;
	EXPECT_EQ(Slurp(r.host_file.parent_path() / name), MinimalIso());
	CDROM_Interface_Image cd = {};
	EXPECT_TRUE(cd.SetDevice(r.host_file.string().c_str()));
}

TEST_F(ArchiveImagesTest, ACueNamingAHostFileIsRefused)
{
	const auto result = Resolve("cd/evil.cue");
	EXPECT_FALSE(result.resolved);
	EXPECT_EQ(result.verdict.reason, DenyReason::ArchiveCueRejected);
	EXPECT_NE(result.detail.find("/etc/passwd"), std::string::npos)
	        << result.detail;
}

TEST_F(ArchiveImagesTest, AnMdsIsRefusedAsACompanionFormat)
{
	const auto result = Resolve("cd/game.mds");
	EXPECT_FALSE(result.resolved);
	EXPECT_EQ(result.verdict.reason, DenyReason::ArchiveCueRejected);
}

TEST_F(ArchiveImagesTest, AMissingMemberIsRefused)
{
	const auto result = Resolve("disks/disk9.img");
	EXPECT_FALSE(result.resolved);
	EXPECT_EQ(result.verdict.reason, DenyReason::ArchiveMemberNotFound);
}

TEST_F(ArchiveImagesTest, ADotComponentIsRefusedAsARejectedName)
{
	const auto result = Resolve("cd/../disks/disk1.img");
	EXPECT_FALSE(result.resolved);
	EXPECT_EQ(result.verdict.reason, DenyReason::ArchiveEntryRejected);
}

TEST_F(ArchiveImagesTest, ADiskIsCopiedOnceIntoTheSidecarWithItsDate)
{
	auto first = Resolve("disks/disk1.img");
	ASSERT_TRUE(first.resolved) << first.detail;
	const auto copy = first.resolved->host_file;
	EXPECT_EQ(copy.filename(), "DISK1.IMG");
	EXPECT_EQ(copy.parent_path().filename(), "DISKS");
	EXPECT_EQ(copy.parent_path().parent_path().filename(), ".images");
	EXPECT_EQ(Slurp(copy), FloppyBytes());
	// MSVC's file_clock has no to_sys (Windows, 2026-10-04): read the mtime
	// through stat as archive_reader_tests' MtimeOf does.
	EXPECT_EQ(MtimeOf(copy), 700000000);
	ASSERT_TRUE(first.resolved->hold->image_lock);
	EXPECT_FALSE(first.resolved->hold->lease);

	// The user's writes stay; a second resolve never re-extracts.
	{
		std::fstream out(copy, std::ios::in | std::ios::out | std::ios::binary);
		out.seekp(1024);
		out << "SAVED";
	}
	const auto held_path = copy;
	first.resolved->hold.reset();
	const auto second = Resolve("disks/disk1.img");
	ASSERT_TRUE(second.resolved) << second.detail;
	EXPECT_EQ(second.resolved->host_file, held_path);
	EXPECT_EQ(Slurp(held_path).substr(1024, 5), "SAVED");
}

TEST_F(ArchiveImagesTest, ASecondHolderOfTheSameCopyIsRefusedAsBusy)
{
	const auto first = Resolve("disks/disk1.img");
	ASSERT_TRUE(first.resolved) << first.detail;
	const auto second = Resolve("disks/disk1.img");
	EXPECT_FALSE(second.resolved);
	EXPECT_EQ(second.verdict.reason, DenyReason::SidecarBusy);
	EXPECT_NE(second.detail.find("DISK1.IMG"), std::string::npos)
	        << second.detail;
}

TEST_F(ArchiveImagesTest, AReadOnlyDiskComesFromTheCacheAndMakesNoCopy)
{
	const auto result = Resolve("disks/disk1.img", true);
	ASSERT_TRUE(result.resolved) << result.detail;
	EXPECT_TRUE(result.resolved->hold->lease);
	EXPECT_FALSE(std_fs::exists(tmp / "saves"));
}

TEST_F(ArchiveImagesTest, APlantedLinkInTheImagesDirectoryIsRefused)
{
	auto first = Resolve("disks/disk1.img");
	ASSERT_TRUE(first.resolved) << first.detail;
	const auto images = first.resolved->host_file.parent_path().parent_path();
	first.resolved->hold.reset();
	std::error_code ec = {};
	std_fs::remove_all(images / "DISKS", ec);
	std_fs::create_directory_symlink(tmp, images / "DISKS", ec);
	if (ec) {
		GTEST_SKIP() << "no symlinks here: " << ec.message();
	}
	const auto second = Resolve("disks/disk1.img");
	EXPECT_FALSE(second.resolved);
	EXPECT_EQ(second.verdict.reason, DenyReason::SidecarUnavailable);
	EXPECT_FALSE(std_fs::exists(tmp / "DISK1.IMG"));
}

TEST_F(ArchiveImagesTest, APlantedLinkInPlaceOfTheCopyIsRefused)
{
	auto first = Resolve("disks/disk1.img");
	ASSERT_TRUE(first.resolved) << first.detail;
	const auto copy = first.resolved->host_file;
	first.resolved->hold.reset();
	std::error_code ec = {};
	std_fs::remove(copy, ec);
	std::ofstream(tmp / "victim") << "keep";
	std_fs::create_symlink(tmp / "victim", copy, ec);
	if (ec) {
		GTEST_SKIP() << "no symlinks here: " << ec.message();
	}
	const auto second = Resolve("disks/disk1.img");
	EXPECT_FALSE(second.resolved);
	EXPECT_EQ(second.verdict.reason, DenyReason::SidecarUnavailable);
	EXPECT_EQ(Slurp(tmp / "victim"), "keep");
}

TEST_F(ArchiveImagesTest, TheRegistryReleasesByDrive)
{
	auto a = Resolve("disks/disk1.img");
	ASSERT_TRUE(a.resolved) << a.detail;
	ArchiveImages::Keep(0, {a.resolved->hold});
	a.resolved->hold.reset();
	EXPECT_EQ(ArchiveImages::HeldCountForTests(0), 1u);
	EXPECT_FALSE(Resolve("disks/disk1.img").resolved); // still held
	ArchiveImages::Release(0);
	EXPECT_EQ(ArchiveImages::HeldCountForTests(0), 0u);
	EXPECT_TRUE(Resolve("disks/disk1.img").resolved);
}

// ada-ciru item 4: the swap on a -ro or -nosidecar drive serves the next
// disk from the cache too, instead of copying it into a sidecar.
TEST_F(ArchiveImagesTest, ADriveMountedReadOnlyKeepsItsSwapsReadOnly)
{
	auto first = Resolve("disks/disk1.img", true);
	MountOnDrive(2, first, true);
	const auto swapped = SwapInto(2, "disks/disk2.img");
	ASSERT_TRUE(swapped.resolved) << swapped.detail;
	EXPECT_TRUE(swapped.resolved->hold->lease);
	EXPECT_FALSE(swapped.resolved->hold->image_lock);
	EXPECT_FALSE(std_fs::exists(tmp / "saves"));
}

TEST_F(ArchiveImagesTest, AReadOnlyDriveKeepsItsNestedSwapsReadOnly)
{
	// The mode is keyed on the host archive; a chain's host is the outer.
	auto error = std::string();
	ASSERT_TRUE(WriteArchive(tmp / "outer.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"game.zip", Slurp(tmp / "game.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
	auto first = ArchiveImages::Resolve(
	        RequestFor("game.zip/disks/disk1.img", true, tmp / "outer.zip"));
	ASSERT_TRUE(first.resolved) << first.detail;
	EXPECT_TRUE(first.resolved->read_only);
	ArchiveImages::Keep(2, {first.resolved->hold});
	ArchiveImages::DriveMode mode = {};
	mode.archive                  = std_fs::canonical(tmp / "outer.zip");
	mode.read_only                = true;
	ArchiveImages::SetDriveMode(2, mode);
	const auto second = SwapInto(2, "game.zip/disks/disk2.img", tmp / "outer.zip");
	ASSERT_TRUE(second.resolved) << second.detail;
	EXPECT_TRUE(second.resolved->read_only);
	EXPECT_TRUE(second.resolved->hold->lease);
	EXPECT_FALSE(second.resolved->hold->image_lock);
	EXPECT_FALSE(std_fs::exists(tmp / "saves"));
}

// ada-ciru item 2: a drive mounted with -sidecar X keeps its later disks
// in X as well, not in a fresh default sidecar.
TEST_F(ArchiveImagesTest, ADriveWithANamedSidecarKeepsItsSwapsInIt)
{
	const auto named = tmp / "named";
	ASSERT_TRUE(std_fs::create_directories(named));
	auto request         = RequestFor("disks/disk1.img");
	request.sidecar_path = named.string();
	auto first           = ArchiveImages::Resolve(request);
	MountOnDrive(2, first, false, named.string());
	const auto swapped = SwapInto(2, "disks/disk2.img");
	ASSERT_TRUE(swapped.resolved) << swapped.detail;
	EXPECT_EQ(swapped.resolved->host_file,
	          named / ".images" / "DISKS" / "DISK2.IMG");
	EXPECT_FALSE(std_fs::exists(tmp / "saves"));
}

// ada-ciru item 2: the held member is found by the resolved entry, so a
// swap back under another spelling reuses the copy instead of a busy refusal.
TEST_F(ArchiveImagesTest, AHeldMemberIsFoundUnderAnySpelling)
{
	auto first = Resolve("disks/diskette-three.img");
	MountOnDrive(2, first, false);
	const auto kept = first.resolved->hold;
	// As typed with DOS separators and case, and the 8.3 alias the drive
	// shows, which only the entry's DOS name can match.
	for (const auto* spelling : {"DISKS\\DISKETTE-THREE.IMG",
	                             "disks/Diskette-Three.img",
	                             "DISKS\\DISKET~1.IMG",
	                             "disks/disket~1.img"}) {
		const auto again = SwapInto(2, spelling);
		ASSERT_TRUE(again.resolved) << spelling << ": " << again.detail;
		EXPECT_EQ(again.resolved->hold, kept) << spelling;
		EXPECT_EQ(again.resolved->host_file, kept->host_file) << spelling;
		// As the swap does after resolving: the same hold is not
		// listed twice.
		ArchiveImages::Keep(2, {again.resolved->hold});
	}
	EXPECT_EQ(ArchiveImages::HeldCountForTests(2), 1u);
}

TEST_F(ArchiveImagesTest, AHeldMemberOfAnotherArchiveIsNotReused)
{
	// Different bytes: a byte-identical copy would find the first
	// archive's sidecar by its hash (D2) and be refused as busy.
	auto other_disk  = FloppyBytes();
	other_disk[1024] = 'X';
	auto error       = std::string();
	ASSERT_TRUE(WriteArchive(tmp / "other.zip",
	                         Kind::ZipDeflate,
	                         {
	                                 {"disks/", "", true},
	                                 {"disks/disk1.img", other_disk}
        },
	                         nullptr,
	                         &error))
	        << error;
	auto first = Resolve("disks/disk1.img");
	MountOnDrive(2, first, false);
	const auto other = SwapInto(2, "disks/disk1.img", tmp / "other.zip");
	ASSERT_TRUE(other.resolved) << other.detail;
	EXPECT_NE(other.resolved->hold, first.resolved->hold);
	EXPECT_NE(other.resolved->host_file, first.resolved->host_file);
}

// ada-ciru item 1: BOOT's holds belong to the booted drive, so a swap on
// that drive finds the image BOOT holds instead of a busy refusal.
TEST_F(ArchiveImagesTest, AHoldKeptForBootIsFoundByTheSwap)
{
	auto first = Resolve("disks/disk1.img");
	ASSERT_TRUE(first.resolved) << first.detail;
	const auto kept = first.resolved->hold;
	ArchiveImages::KeepForBoot({kept});
	first.resolved.reset();
	const auto again = SwapInto(0, "disks/disk1.img");
	ASSERT_TRUE(again.resolved) << again.detail;
	EXPECT_EQ(again.resolved->hold, kept);
}

// The swap names a member by FindMember's rules: an exact spelling wins
// over a case-folded one, so a held DISK1.IMG is not disk1.img.
TEST_F(ArchiveImagesTest, AHeldMemberIsNotReusedForItsCaseTwin)
{
	auto upper     = FloppyBytes();
	auto lower     = FloppyBytes();
	upper[0]       = 'U';
	lower[0]       = 'l';
	auto error     = std::string();
	const auto zip = tmp / "twins.zip";
	ASSERT_TRUE(WriteArchive(zip,
	                         Kind::ZipDeflate,
	                         {
	                                 {"DISK1.IMG", upper},
                                         {"disk1.img", lower}
        },
	                         nullptr,
	                         &error))
	        << error;
	auto first = ArchiveImages::Resolve(RequestFor("DISK1.IMG", false, zip));
	ASSERT_TRUE(first.resolved) << first.detail;
	ArchiveImages::Keep(0, {first.resolved->hold});
	const auto again = SwapInto(0, "disk1.img", zip);
	ASSERT_TRUE(again.resolved) << again.detail;
	EXPECT_NE(again.resolved->hold, first.resolved->hold);
	EXPECT_EQ(Slurp(again.resolved->host_file).substr(0, 1), "l");
}

// BOOT's set cycles through A and B (swapInDisks) whatever drive it boots
// from: `boot d1.img d2.img` puts d2 in B, `-l c` boots a hard disk.
TEST_F(ArchiveImagesTest, BootsHoldsAreFoundByASwapOnAOrB)
{
	auto first = Resolve("disks/disk1.img");
	ASSERT_TRUE(first.resolved) << first.detail;
	const auto kept = first.resolved->hold;
	ArchiveImages::KeepForBoot({kept});
	first.resolved.reset();
	for (const int drive : {0, 1}) {
		const auto again = SwapInto(drive, "disks/disk1.img");
		ASSERT_TRUE(again.resolved)
		        << "swap " << drive << ": " << again.detail;
		EXPECT_EQ(again.resolved->hold, kept);
	}
}

// A cache file is shared by every engine that leases the entry, so a disk
// swapped in from it on a -ro drive must not take writes.
TEST_F(ArchiveImagesTest, ARawSwapOnAReadOnlyDriveCannotWriteTheCache)
{
	auto first = Resolve("disks/disk1.img", true);
	ASSERT_TRUE(first.resolved) << first.detail;
	auto read_only  = true;
	const auto host = first.resolved->host_file.string();
	auto* file      = fopen_wrap_ro_fallback(host, read_only);
	ASSERT_NE(file, nullptr);
	imageDiskList[0] = std::make_shared<imageDisk>(file, host.c_str(), 320, false);
	MountOnDrive(0, first, true);

	const auto swapped = DriveSwap::Swap('A',
	                                     tmp / "game.zip" / "disks" / "disk2.img",
	                                     false,
	                                     MountPolicy::ConfAnchor(),
	                                     {tmp});
	ASSERT_TRUE(swapped.ok) << swapped.error;
	const auto cached = std_fs::path(imageDiskList[0]->diskname);
	ASSERT_NE(cached.string(), host);
	const auto before = Slurp(cached);
	auto sector       = std::array<uint8_t, 512>{};
	sector.fill(0x5A);
	EXPECT_NE(imageDiskList[0]->Write_AbsoluteSector(3, sector.data()), 0);
	imageDiskList.fill(nullptr);
	EXPECT_TRUE(Slurp(cached) == before);
}

TEST_F(ArchiveImagesTest, ReleaseForgetsTheDriveMode)
{
	auto first = Resolve("disks/disk1.img", true);
	MountOnDrive(2, first, true);
	ASSERT_TRUE(ArchiveImages::DriveModeOf(2));
	EXPECT_TRUE(ArchiveImages::DriveModeOf(2)->read_only);
	ArchiveImages::Release(2);
	EXPECT_FALSE(ArchiveImages::DriveModeOf(2));
	EXPECT_FALSE(ArchiveImages::DriveModeOf(3));
}

// A path inside base, by the names alone.
static bool IsInside(const std_fs::path& path, const std_fs::path& base)
{
	const auto rel = path.lexically_relative(base);
	return !rel.empty() && *rel.begin() != "..";
}

// Phase 6.4: a scratch drive's images copy into the scratch, never into
// a sidecar.
TEST_F(ArchiveImagesTest, AScratchRequestCopiesTheImageIntoTheScratch)
{
	// Outside the cache: a store refuses a directory it did not tag.
	const auto scratch = tmp / "scratch-ab";
	ASSERT_TRUE(std_fs::create_directories(scratch));
	auto request        = RequestFor("disks/disk1.img");
	request.scratch_dir = scratch;
	const auto got      = ArchiveImages::Resolve(request);
	ASSERT_TRUE(got.resolved) << got.detail;
	EXPECT_TRUE(IsInside(got.resolved->host_file, scratch))
	        << got.resolved->host_file;
	EXPECT_EQ(got.resolved->host_file.filename(), "DISK1.IMG");
	EXPECT_TRUE(got.resolved->hold->image_lock);
	EXPECT_FALSE(got.resolved->read_only);
	EXPECT_FALSE(std_fs::exists(tmp / "saves"));
}

// Every swap on a scratch drive lands in the scratch, another archive's
// disk included: the drive has no saves to protect and never grows a
// sidecar. Two archives with one member path get two copies.
TEST_F(ArchiveImagesTest, AScratchDriveTakesEverySwapIntoTheScratch)
{
	const auto scratch = tmp / "scratch-ab";
	ASSERT_TRUE(std_fs::create_directories(scratch));
	auto request        = RequestFor("disks/disk1.img");
	request.scratch_dir = scratch;
	auto first          = ArchiveImages::Resolve(request);
	ASSERT_TRUE(first.resolved) << first.detail;
	ArchiveImages::Keep(2, {first.resolved->hold});
	ArchiveImages::DriveMode mode = {};
	mode.archive                  = std_fs::canonical(tmp / "game.zip");
	mode.scratch_dir              = scratch;
	ArchiveImages::SetDriveMode(2, mode);
	const auto swapped = SwapInto(2, "disks/disk2.img");
	ASSERT_TRUE(swapped.resolved) << swapped.detail;
	EXPECT_TRUE(IsInside(swapped.resolved->host_file, scratch))
	        << swapped.resolved->host_file;
	EXPECT_EQ(swapped.resolved->host_file.filename(), "DISK2.IMG");
	auto other_disk  = FloppyBytes();
	other_disk[1024] = 'X';
	auto error       = std::string();
	ASSERT_TRUE(WriteArchive(tmp / "other.zip",
	                         Kind::ZipDeflate,
	                         {
	                                 {"disks/", "", true},
	                                 {"disks/disk1.img", other_disk}
        },
	                         nullptr,
	                         &error))
	        << error;
	const auto other = SwapInto(2, "disks/disk1.img", tmp / "other.zip");
	ASSERT_TRUE(other.resolved) << other.detail;
	EXPECT_TRUE(IsInside(other.resolved->host_file, scratch))
	        << other.resolved->host_file;
	EXPECT_NE(other.resolved->host_file, first.resolved->host_file);
	EXPECT_EQ(Slurp(other.resolved->host_file), other_disk);
	EXPECT_EQ(Slurp(first.resolved->host_file), FloppyBytes());
	EXPECT_FALSE(std_fs::exists(tmp / "saves"));
}

// Every file and directory under dir, as relative paths; empty when dir is
// missing.
static std::vector<std::string> TreeOf(const std_fs::path& dir)
{
	auto out           = std::vector<std::string>{};
	std::error_code ec = {};
	auto it            = std_fs::recursive_directory_iterator(dir, ec);
	for (; !ec && it != std_fs::recursive_directory_iterator();
	     it.increment(ec)) {
		out.push_back(it->path().lexically_relative(dir).generic_string());
	}
	std::sort(out.begin(), out.end());
	return out;
}

// ada-lm3d: a member that is no disk image is checked before it is placed,
// and a refusal leaves no trace in the sidecar or the scratch.
class NonImageMemberTest : public ArchiveImagesTest {
protected:
	std_fs::path Bundle()
	{
		auto error = std::string();
		EXPECT_TRUE(WriteArchive(tmp / "bundle.zip",
		                         Kind::ZipDeflate,
		                         {
		                                 {"disk1.img", FloppyBytes()},
		                                 {"inner.zip", std::string(5000, 'z')},
		                                 {"readme.txt", "hello\r\n"}
                },
		                         nullptr,
		                         &error))
		        << error;
		return tmp / "bundle.zip";
	}
};

TEST_F(NonImageMemberTest, ARefusalLeavesNoSidecarBehind)
{
	const auto bundle = Bundle();
	for (const auto* member : {"inner.zip", "readme.txt"}) {
		const auto result = ArchiveImages::Resolve(
		        RequestFor(member, false, bundle));
		EXPECT_FALSE(result.resolved) << member;
		EXPECT_EQ(result.verdict.reason, DenyReason::NotADiskImage) << member;
		EXPECT_TRUE(TreeOf(tmp / "saves").empty()) << member;
	}
}

TEST_F(NonImageMemberTest, ARefusalLeavesAnExistingSidecarAsItWas)
{
	const auto bundle = Bundle();
	auto disk = ArchiveImages::Resolve(RequestFor("disk1.img", false, bundle));
	ASSERT_TRUE(disk.resolved) << disk.detail;
	const auto before = TreeOf(tmp / "saves");
	ASSERT_FALSE(before.empty());
	const auto result = ArchiveImages::Resolve(
	        RequestFor("inner.zip", false, bundle));
	EXPECT_FALSE(result.resolved);
	EXPECT_EQ(result.verdict.reason, DenyReason::NotADiskImage);
	EXPECT_EQ(TreeOf(tmp / "saves"), before);
}

// A sidecar whose record is gone is found again as "to create"; a
// refusal must still leave it and its saves alone.
TEST_F(NonImageMemberTest, ARefusalKeepsASidecarWithoutItsRecord)
{
	const auto bundle = Bundle();
	auto disk = ArchiveImages::Resolve(RequestFor("disk1.img", false, bundle));
	ASSERT_TRUE(disk.resolved) << disk.detail;
	disk.resolved.reset();
	auto sidecars = std::vector<std_fs::path>{};
	for (const auto& e : std_fs::directory_iterator(tmp / "saves")) {
		sidecars.push_back(e.path());
	}
	ASSERT_EQ(sidecars.size(), 1u);
	const auto sidecar = sidecars.front();
	ASSERT_TRUE(std_fs::remove(sidecar / ".source-archive.toml"));
	std::ofstream(sidecar / "GAME.SAV") << "level 7";
	const auto result = ArchiveImages::Resolve(
	        RequestFor("inner.zip", false, bundle));
	EXPECT_FALSE(result.resolved);
	EXPECT_TRUE(std_fs::is_regular_file(sidecar / "GAME.SAV"));
}

TEST_F(NonImageMemberTest, ARefusalLeavesTheScratchEmpty)
{
	const auto bundle  = Bundle();
	const auto scratch = tmp / "scratch-ab";
	ASSERT_TRUE(std_fs::create_directories(scratch));
	auto request        = RequestFor("inner.zip", false, bundle);
	request.scratch_dir = scratch;
	const auto result   = ArchiveImages::Resolve(request);
	EXPECT_FALSE(result.resolved);
	EXPECT_EQ(result.verdict.reason, DenyReason::NotADiskImage);
	EXPECT_TRUE(TreeOf(scratch).empty());
	EXPECT_TRUE(TreeOf(tmp / "saves").empty());
}

TEST_F(ArchiveImagesTest, ReleasingAHoldRemovesItsScratchDirectory)
{
	auto result = Resolve("cd/game.cue");
	ASSERT_TRUE(result.resolved) << result.detail;
	const auto scratch = result.resolved->hold->scratch_dir;
	ASSERT_TRUE(std_fs::exists(scratch));
	result.resolved.reset();
	EXPECT_FALSE(std_fs::exists(scratch));
}

TEST_F(ArchiveImagesTest, TheHelperIsGoneAfterResolve)
{
	const auto result = Resolve("cd/game.iso");
	ASSERT_TRUE(result.resolved) << result.detail;
	EXPECT_EQ(ArchiveMount::ArchiveHelperClient::LiveHelpers(), 0);
}

// --- Nested archives (Phase 1) ---

TEST_F(ArchiveImagesTest, ANestedDiskResolvesThroughTheChain)
{
	auto error = std::string();
	ASSERT_TRUE(WriteArchive(tmp / "outer.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"game.zip", Slurp(tmp / "game.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
	auto result = ArchiveImages::Resolve(
	        RequestFor("game.zip/disks/disk1.img", false, tmp / "outer.zip"));
	ASSERT_TRUE(result.resolved) << result.detail;
	EXPECT_EQ(result.resolved->member_name, "disks/disk1.img");
	EXPECT_EQ(result.resolved->size, 327680);
	EXPECT_EQ(Slurp(result.resolved->host_file), FloppyBytes());
	// The level helpers close with the host's; the hold keeps the leases.
	EXPECT_EQ(ArchiveMount::ArchiveHelperClient::LiveHelpers(), 0);
}

TEST_F(ArchiveImagesTest, ANestedDiskInAStoredChainMakesNoCacheEntry)
{
	// A stored outer around the fixture's game.zip: the level is a window
	// (nested design 2.1), the disk copies out of it into the sidecar and
	// the cache never sees the inner zip.
	auto error = std::string();
	ASSERT_TRUE(WriteArchive(tmp / "outer.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"game.zip", Slurp(tmp / "game.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
	auto result = ArchiveImages::Resolve(
	        RequestFor("game.zip/disks/disk1.img", false, tmp / "outer.zip"));
	ASSERT_TRUE(result.resolved) << result.detail;
	EXPECT_EQ(Slurp(result.resolved->host_file), FloppyBytes());
	ASSERT_EQ(result.resolved->hold->chain_links.size(), 1u);
	EXPECT_TRUE(result.resolved->hold->chain_links[0].window);
	EXPECT_FALSE(result.resolved->hold->chain_links[0].lease);
	auto tomls = 0;
	for (const auto& item : std_fs::recursive_directory_iterator(tmp / "cache")) {
		if (item.path().filename() == ArchiveMount::EntryTomlName) {
			++tomls;
		}
	}
	EXPECT_EQ(tomls, 0);
	EXPECT_EQ(ArchiveMount::ArchiveHelperClient::LiveHelpers(), 0);
}

TEST_F(ArchiveImagesTest, ACdInAWindowedZipIsCachedUnderTheInnerZipsName)
{
	auto error = std::string();
	ASSERT_TRUE(WriteArchive(tmp / "cd.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"game.iso", MinimalIso()}
        },
	                         nullptr,
	                         &error))
	        << error;
	ASSERT_TRUE(WriteArchive(tmp / "outer.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"cd.zip", Slurp(tmp / "cd.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
	auto result = ArchiveImages::Resolve(
	        RequestFor("cd.zip/game.iso", true, tmp / "outer.zip"));
	ASSERT_TRUE(result.resolved) << result.detail;
	// The leaf lease's entry is named after cd.zip, not after outer.zip
	// and not after a cache copy.
	const auto entry_dir = result.resolved->host_file.parent_path().parent_path();
	EXPECT_EQ(entry_dir.filename().string().rfind("cd-", 0), 0u)
	        << entry_dir.string();
	EXPECT_NE(entry_dir.string().find((tmp / "cache").string()),
	          std::string::npos);
}

TEST_F(ArchiveImagesTest, ASwapBackToANestedDiskFindsItsHold)
{
	auto error = std::string();
	ASSERT_TRUE(WriteArchive(tmp / "outer.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"game.zip", Slurp(tmp / "game.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
	const auto first = SwapInto(2, "game.zip/disks/disk1.img", tmp / "outer.zip");
	ASSERT_TRUE(first.resolved) << first.detail;
	ArchiveImages::Keep(2, {first.resolved->hold});
	const auto other = SwapInto(2, "game.zip/disks/disk2.img", tmp / "outer.zip");
	ASSERT_TRUE(other.resolved) << other.detail;
	ArchiveImages::Keep(2, {other.resolved->hold});
	const auto again = SwapInto(2, "game.zip/disks/disk1.img", tmp / "outer.zip");
	ASSERT_TRUE(again.resolved) << again.detail;
	EXPECT_EQ(again.resolved->host_file, first.resolved->host_file);
	EXPECT_EQ(again.resolved->hold.get(), first.resolved->hold.get());
}

TEST_F(ArchiveImagesTest, AHostDiskIsNotServedFromANestedHoldOfTheSameName)
{
	auto outer_disk    = FloppyBytes();
	outer_disk[300000] = 'O';
	auto inner_disk    = FloppyBytes();
	inner_disk[300000] = 'I';
	auto error         = std::string();
	ASSERT_TRUE(WriteArchive(tmp / "inner.zip",
	                         Kind::ZipDeflate,
	                         {
	                                 {"disk1.img", inner_disk}
        },
	                         nullptr,
	                         &error))
	        << error;
	// Both archives hold "disk1.img" at ordinal 0.
	ASSERT_TRUE(WriteArchive(tmp / "pair.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"disk1.img",               outer_disk},
	                                 {"inner.zip", Slurp(tmp / "inner.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
	const auto nested = SwapInto(2, "inner.zip/disk1.img", tmp / "pair.zip");
	ASSERT_TRUE(nested.resolved) << nested.detail;
	ArchiveImages::Keep(2, {nested.resolved->hold});
	EXPECT_EQ(Slurp(nested.resolved->host_file)[300000], 'I');
	const auto host = SwapInto(2, "disk1.img", tmp / "pair.zip");
	ASSERT_TRUE(host.resolved) << host.detail;
	EXPECT_NE(host.resolved->host_file, nested.resolved->host_file);
	EXPECT_EQ(Slurp(host.resolved->host_file)[300000], 'O');
}

TEST_F(ArchiveImagesTest, ANestedDisksSidecarCarriesTheLeafIdentityUnderTheHostsName)
{
	auto error = std::string();
	ASSERT_TRUE(WriteArchive(tmp / "outer.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"game.zip", Slurp(tmp / "game.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
	const auto result = ArchiveImages::Resolve(
	        RequestFor("game.zip/disks/disk1.img", false, tmp / "outer.zip"));
	ASSERT_TRUE(result.resolved) << result.detail;
	// The copy lives in a sidecar under saves, never inside the cache.
	const auto cache_dir = std_fs::weakly_canonical(tmp / "cache");
	EXPECT_NE(result.resolved->host_file.string().find((tmp / "saves").string()),
	          std::string::npos)
	        << result.resolved->host_file;
	auto records = 0;
	for (const auto& entry : std_fs::recursive_directory_iterator(tmp / "saves")) {
		if (entry.path().filename() != ArchiveMount::SidecarTomlName) {
			continue;
		}
		++records;
		auto detail = std::string();
		const auto record = ArchiveMount::SidecarRecord::Load(entry.path(),
		                                                      &detail);
		ASSERT_TRUE(record) << detail;
		EXPECT_EQ(record->archive_path,
		          ArchiveMount::PathToUtf8(tmp / "outer.zip") + "/game.zip");
		EXPECT_EQ(record->archive_path.find(cache_dir.string()),
		          std::string::npos);
		// N5: the leaf archive's identity, the host file's name.
		// game.zip is a stored member of a stored outer, so its window
		// identity is its file identity (Phase 2).
		const auto leaf_hash =
		        ArchiveMount::ArchiveReader::Open(tmp / "game.zip",
		                                          ArchiveMount::Limits{})
		                .reader->IdentityHash();
		EXPECT_EQ(record->identity_hash, leaf_hash);
		EXPECT_EQ(entry.path().parent_path().filename().string(),
		          "outer-" + leaf_hash.substr(0, 8));
	}
	EXPECT_EQ(records, 1);
}

TEST_F(ArchiveImagesTest, AShortMemberPathEntersAHouseArchive)
{
	// house.zip holds info.toml and the fixture's game.zip under the
	// house name; disks/disk1.img is inside the entered archive (N4).
	auto error = std::string();
	ASSERT_TRUE(WriteArchive(tmp / "house.zip",
	                         Kind::ZipStored,
	                         {
	                                 {     "info.toml", "[game]\nname = \"x\"\n"},
	                                 {"game.store.zip",  Slurp(tmp / "game.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
	auto result = ArchiveImages::Resolve(
	        RequestFor("disks/disk1.img", false, tmp / "house.zip"));
	ASSERT_TRUE(result.resolved) << result.detail;
	EXPECT_EQ(result.resolved->member_name, "disks/disk1.img");
	EXPECT_EQ(Slurp(result.resolved->host_file), FloppyBytes());
	EXPECT_EQ(result.resolved->hold->chain, "game.store.zip");
	EXPECT_EQ(result.resolved->hold->spelled_chain, "");
	// The full form names the same thing and shares the chain.
	auto full = ArchiveImages::Resolve(
	        RequestFor("game.store.zip/disks/disk2.img", false, tmp / "house.zip"));
	ASSERT_TRUE(full.resolved) << full.detail;
	EXPECT_EQ(full.resolved->hold->chain, "game.store.zip");
	EXPECT_EQ(full.resolved->hold->spelled_chain, "game.store.zip");
}

TEST_F(ArchiveImagesTest, AShortPathIntoAWildArchiveIsStillNotFound)
{
	// An inner zip beside a readme is a file on the drive, not a door.
	auto error = std::string();
	ASSERT_TRUE(WriteArchive(tmp / "wild.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"readme.txt",                 "hello"},
	                                 {  "game.zip", Slurp(tmp / "game.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
	auto result = ArchiveImages::Resolve(
	        RequestFor("disks/disk1.img", false, tmp / "wild.zip"));
	ASSERT_FALSE(result.resolved);
	EXPECT_EQ(result.verdict.reason, DenyReason::ArchiveMemberNotFound);
}

TEST_F(ArchiveImagesTest, ASwapBackThroughTheShortPathFindsItsHold)
{
	auto error = std::string();
	ASSERT_TRUE(WriteArchive(tmp / "house.zip",
	                         Kind::ZipStored,
	                         {
	                                 {     "info.toml",              "[game]\n"},
	                                 {"game.store.zip", Slurp(tmp / "game.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
	const auto first = SwapInto(2, "disks/disk1.img", tmp / "house.zip");
	ASSERT_TRUE(first.resolved) << first.detail;
	ArchiveImages::Keep(2, {first.resolved->hold});
	const auto other = SwapInto(2, "disks/disk2.img", tmp / "house.zip");
	ASSERT_TRUE(other.resolved) << other.detail;
	ArchiveImages::Keep(2, {other.resolved->hold});
	const auto again = SwapInto(2, "disks/disk1.img", tmp / "house.zip");
	ASSERT_TRUE(again.resolved) << again.detail;
	EXPECT_EQ(again.resolved->hold.get(), first.resolved->hold.get());
	EXPECT_EQ(ArchiveMount::ArchiveHelperClient::LiveHelpers(), 0);
}

// G11, Bard's Tale III: the house store holds a TDC zip, the disks are in
// that zip, and the short path spells the TDC zip but not the store.
static void WriteHouseWithInnerZip(const std_fs::path& tmp)
{
	auto error = std::string();
	ASSERT_TRUE(WriteArchive(tmp / "store.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"tdc.zip", Slurp(tmp / "game.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
	ASSERT_TRUE(WriteArchive(tmp / "house.zip",
	                         Kind::ZipStored,
	                         {
	                                 {     "info.toml",               "[game]\n"},
	                                 {"game.store.zip", Slurp(tmp / "store.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
}

TEST_F(ArchiveImagesTest, AShortPathThroughAFurtherArchiveEntersTheHouse)
{
	WriteHouseWithInnerZip(tmp);
	auto result = ArchiveImages::Resolve(
	        RequestFor("tdc.zip/disks/disk1.img", false, tmp / "house.zip"));
	ASSERT_TRUE(result.resolved) << result.detail;
	EXPECT_EQ(Slurp(result.resolved->host_file), FloppyBytes());
	EXPECT_EQ(result.resolved->hold->chain, "game.store.zip/tdc.zip");
	EXPECT_EQ(result.resolved->hold->spelled_chain, "tdc.zip");
}

TEST_F(ArchiveImagesTest, ASwapBackThroughAShortChainFindsItsHold)
{
	WriteHouseWithInnerZip(tmp);
	const auto first = SwapInto(2, "tdc.zip/disks/disk1.img", tmp / "house.zip");
	ASSERT_TRUE(first.resolved) << first.detail;
	ArchiveImages::Keep(2, {first.resolved->hold});
	const auto other = SwapInto(2, "tdc.zip/disks/disk2.img", tmp / "house.zip");
	ASSERT_TRUE(other.resolved) << other.detail;
	ArchiveImages::Keep(2, {other.resolved->hold});
	const auto again = SwapInto(2, "tdc.zip/disks/disk1.img", tmp / "house.zip");
	ASSERT_TRUE(again.resolved) << again.detail;
	EXPECT_EQ(again.resolved->hold.get(), first.resolved->hold.get());
}

TEST_F(ArchiveImagesTest, AShortChainIntoAWildArchiveIsStillNotFound)
{
	auto error = std::string();
	ASSERT_TRUE(WriteArchive(tmp / "wild.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"readme.txt",                 "hello"},
	                                 {  "game.zip", Slurp(tmp / "game.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
	auto result = ArchiveImages::Resolve(
	        RequestFor("tdc.zip/disks/disk1.img", false, tmp / "wild.zip"));
	ASSERT_FALSE(result.resolved);
	EXPECT_NE(result.detail.find("no such member"), std::string::npos)
	        << result.detail;
}

TEST_F(ArchiveImagesTest, ADiskInsideAZipInsideAnIsoResolves)
{
	const auto iso = ArchiveFixtures::IsoWithFiles({
	        {"GAMES/GAME.ZIP", Slurp(tmp / "game.zip")}
        });
	ArchiveFixtures::WriteBytes(tmp / "disc.iso",
	                            std::vector<uint8_t>(iso.begin(), iso.end()));
	auto result = ArchiveImages::Resolve(
	        RequestFor("GAMES/GAME.ZIP/disks/disk1.img", false, tmp / "disc.iso"));
	ASSERT_TRUE(result.resolved) << result.detail;
	EXPECT_EQ(result.resolved->member_name, "disks/disk1.img");
	EXPECT_EQ(Slurp(result.resolved->host_file), FloppyBytes());
	EXPECT_EQ(result.resolved->hold->chain, "GAMES/GAME.ZIP");
	EXPECT_EQ(ArchiveMount::ArchiveHelperClient::LiveHelpers(), 0);
}

// G16: a house archive on a disc is a chain level, not the host file.
static void WriteIsoWith(const std_fs::path& tmp, const std_fs::path& archive)
{
	const auto iso = ArchiveFixtures::IsoWithFiles({
	        {"GAMES/HOUSE.ZIP", Slurp(archive)}
        });
	ArchiveFixtures::WriteBytes(tmp / "disc.iso",
	                            std::vector<uint8_t>(iso.begin(), iso.end()));
}

static void WriteHouse(const std_fs::path& tmp)
{
	auto error = std::string();
	ASSERT_TRUE(WriteArchive(tmp / "house.zip",
	                         Kind::ZipStored,
	                         {
	                                 {     "info.toml",              "[game]\n"},
	                                 {"game.store.zip", Slurp(tmp / "game.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
}

TEST_F(ArchiveImagesTest, AHouseArchiveOnADiscEntersItsStore)
{
	WriteHouse(tmp);
	WriteIsoWith(tmp, tmp / "house.zip");
	auto result = ArchiveImages::Resolve(
	        RequestFor("GAMES/HOUSE.ZIP/disks/disk1.img", false, tmp / "disc.iso"));
	ASSERT_TRUE(result.resolved) << result.detail;
	EXPECT_EQ(result.resolved->member_name, "disks/disk1.img");
	EXPECT_EQ(Slurp(result.resolved->host_file), FloppyBytes());
	EXPECT_EQ(result.resolved->hold->chain, "GAMES/HOUSE.ZIP/game.store.zip");
	EXPECT_EQ(result.resolved->hold->spelled_chain, "GAMES/HOUSE.ZIP");
	EXPECT_EQ(ArchiveMount::ArchiveHelperClient::LiveHelpers(), 0);
}

TEST_F(ArchiveImagesTest, AShortChainThroughAHouseArchiveOnADiscEntersItsStore)
{
	WriteHouseWithInnerZip(tmp);
	WriteIsoWith(tmp, tmp / "house.zip");
	auto result = ArchiveImages::Resolve(
	        RequestFor("GAMES/HOUSE.ZIP/tdc.zip/disks/disk1.img",
	                   false,
	                   tmp / "disc.iso"));
	ASSERT_TRUE(result.resolved) << result.detail;
	EXPECT_EQ(Slurp(result.resolved->host_file), FloppyBytes());
	EXPECT_EQ(result.resolved->hold->chain,
	          "GAMES/HOUSE.ZIP/game.store.zip/tdc.zip");
}

TEST_F(ArchiveImagesTest, ASwapBackThroughAHouseArchiveOnADiscFindsItsHold)
{
	WriteHouse(tmp);
	WriteIsoWith(tmp, tmp / "house.zip");
	const auto first = SwapInto(2,
	                            "GAMES/HOUSE.ZIP/disks/disk1.img",
	                            tmp / "disc.iso");
	ASSERT_TRUE(first.resolved) << first.detail;
	ArchiveImages::Keep(2, {first.resolved->hold});
	const auto other = SwapInto(2,
	                            "GAMES/HOUSE.ZIP/disks/disk2.img",
	                            tmp / "disc.iso");
	ASSERT_TRUE(other.resolved) << other.detail;
	ArchiveImages::Keep(2, {other.resolved->hold});
	const auto again = SwapInto(2,
	                            "GAMES/HOUSE.ZIP/disks/disk1.img",
	                            tmp / "disc.iso");
	ASSERT_TRUE(again.resolved) << again.detail;
	EXPECT_EQ(again.resolved->hold.get(), first.resolved->hold.get());
}

TEST_F(ArchiveImagesTest, AWildArchiveOnADiscIsStillNotFound)
{
	auto error = std::string();
	ASSERT_TRUE(WriteArchive(tmp / "wild.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"readme.txt",                 "hello"},
	                                 {  "game.zip", Slurp(tmp / "game.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
	WriteIsoWith(tmp, tmp / "wild.zip");
	auto result = ArchiveImages::Resolve(
	        RequestFor("GAMES/HOUSE.ZIP/disks/disk1.img", false, tmp / "disc.iso"));
	ASSERT_FALSE(result.resolved);
	EXPECT_EQ(result.verdict.reason, DenyReason::ArchiveMemberNotFound);
}

// A store shaped like a house archive itself: the rule enters one store.
static void WriteHouseInHouse(const std_fs::path& tmp)
{
	auto error = std::string();
	ASSERT_TRUE(WriteArchive(tmp / "store.zip",
	                         Kind::ZipStored,
	                         {
	                                 {     "info.toml",              "[game]\n"},
	                                 {"deep.store.zip", Slurp(tmp / "game.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
	ASSERT_TRUE(WriteArchive(tmp / "house.zip",
	                         Kind::ZipStored,
	                         {
	                                 {     "info.toml",               "[game]\n"},
	                                 {"game.store.zip", Slurp(tmp / "store.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
}

TEST_F(ArchiveImagesTest, AStoreTheHostEnteredIsNotSearchedForAnother)
{
	WriteHouseInHouse(tmp);
	auto result = ArchiveImages::Resolve(
	        RequestFor("disks/disk1.img", false, tmp / "house.zip"));
	ASSERT_FALSE(result.resolved);
	EXPECT_EQ(result.verdict.reason, DenyReason::ArchiveMemberNotFound);
}

TEST_F(ArchiveImagesTest, AStoreOnADiscIsNotSearchedForAnother)
{
	WriteHouseInHouse(tmp);
	WriteIsoWith(tmp, tmp / "house.zip");
	auto result = ArchiveImages::Resolve(
	        RequestFor("GAMES/HOUSE.ZIP/disks/disk1.img", false, tmp / "disc.iso"));
	ASSERT_FALSE(result.resolved);
	EXPECT_EQ(result.verdict.reason, DenyReason::ArchiveMemberNotFound);
}

TEST_F(ArchiveImagesTest, AStoreBelowTheDeepestLevelIsRefused)
{
	// Three spelled levels are the cap; the store would be a fourth.
	WriteHouse(tmp);
	auto error = std::string();
	ASSERT_TRUE(WriteArchive(tmp / "b.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"house.zip", Slurp(tmp / "house.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
	ASSERT_TRUE(WriteArchive(tmp / "a.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"b.zip", Slurp(tmp / "b.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
	ASSERT_TRUE(WriteArchive(tmp / "outer.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"a.zip", Slurp(tmp / "a.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
	auto result = ArchiveImages::Resolve(RequestFor(
	        "a.zip/b.zip/house.zip/disks/disk1.img", false, tmp / "outer.zip"));
	ASSERT_FALSE(result.resolved);
	EXPECT_EQ(result.verdict.reason, DenyReason::ArchiveEntryRejected);
	EXPECT_NE(result.detail.find("nesting depth"), std::string::npos)
	        << result.detail;
	EXPECT_EQ(ArchiveMount::ArchiveHelperClient::LiveHelpers(), 0);
}

TEST_F(ArchiveImagesTest, ADiskInsideAZipWithALongJolietNameResolves)
{
	// G17: King's Quest IV's zip on the disc has a 104-character name.
	if (!ArchiveFixtures::LibarchiveReadsJolietName(104)) {
		GTEST_SKIP() << "warning: the linked libarchive cuts Joliet names "
		                "at 103 characters (unpatched system library)";
	}
	const auto name = std::string(100, 'k') + ".zip";
	const auto iso  = ArchiveFixtures::IsoWithJolietFile(name,
                                                            Slurp(tmp / "game.zip"));
	ArchiveFixtures::WriteBytes(tmp / "disc.iso",
	                            std::vector<uint8_t>(iso.begin(), iso.end()));
	auto result = ArchiveImages::Resolve(
	        RequestFor(name + "/disks/disk1.img", false, tmp / "disc.iso"));
	ASSERT_TRUE(result.resolved) << result.detail;
	EXPECT_EQ(Slurp(result.resolved->host_file), FloppyBytes());
	EXPECT_EQ(result.resolved->hold->chain, name);
}

TEST_F(ArchiveImagesTest, AnIsoLeafInsideAChainIsStillACdImage)
{
	auto error = std::string();
	ASSERT_TRUE(WriteArchive(tmp / "outer.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"game.zip", Slurp(tmp / "game.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
	auto result = ArchiveImages::Resolve(
	        RequestFor("game.zip/cd/game.iso", false, tmp / "outer.zip"));
	ASSERT_TRUE(result.resolved) << result.detail;
	EXPECT_EQ(result.resolved->hold->chain, "game.zip");
	EXPECT_TRUE(result.resolved->read_only); // a CD image comes from the cache
	EXPECT_TRUE(result.resolved->hold->lease);
	EXPECT_EQ(ArchiveImages::KindOf(result.resolved->member_name),
	          ArchiveImages::Kind::CdImage);
}

TEST_F(ArchiveImagesTest, AFourthNestingLevelIsRefused)
{
	auto error = std::string();
	ASSERT_TRUE(WriteArchive(tmp / "outer.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"a.zip", Slurp(tmp / "game.zip")}
        },
	                         nullptr,
	                         &error))
	        << error;
	const auto result = ArchiveImages::Resolve(RequestFor(
	        "a.zip/b.zip/c.zip/d.zip/disk1.img", false, tmp / "outer.zip"));
	EXPECT_FALSE(result.resolved);
	EXPECT_FALSE(result.verdict.allowed);
	EXPECT_EQ(result.verdict.reason, DenyReason::ArchiveEntryRejected);
	EXPECT_NE(result.detail.find("nesting depth"), std::string::npos)
	        << result.detail;
}

} // namespace
