// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dos/programs/mount.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "dos/drive_swap.h"
#include "dos/drives.h"
#include "dos/programs/mount_policy.h"
#include "dosbox_test_fixture.h"
#include "ints/bios_disk.h"
#include "misc/cross.h"

namespace {

namespace std_fs = std::filesystem;

class MountTest : public DOSBoxTestFixture {
protected:
	static std_fs::path test_file_path;

	static void write_file(const std_fs::path& path,
	                       size_t num_bytes = 1024, char fill = 0)
	{
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		std::vector<char> buf(num_bytes, fill);
		out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
	}

	// The automation fork validates image structure before mounting
	// (mount_policy ValidateDiskImageStructure), so image fixtures
	// need a FAT boot signature, ISO 9660 magic, a known floppy
	// size, or a cue FILE line to get past ProcessPaths.

	static void write_fat_image(const std_fs::path& path, size_t num_bytes)
	{
		std::vector<char> buf(num_bytes, 0);
		buf[510] = '\x55';
		buf[511] = '\xAA';
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
	}

	static void write_iso_image(const std_fs::path& path)
	{
		// "CD001" at 0x8001: volume descriptor magic per ECMA-119
		std::vector<char> buf(0x9000, 0);
		const char magic[] = {'C', 'D', '0', '0', '1'};
		std::copy(std::begin(magic), std::end(magic), buf.begin() + 0x8001);
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
	}

	// 320K DOS 1.x floppy: no BPB, fatDrive synthesizes one from the
	// media descriptor at the start of the FAT (sector 1). The stub
	// fat images above fail the swap path, which autodetects geometry.
	static void write_dos1_floppy(const std_fs::path& path)
	{
		std::vector<char> buf(327680, 0);
		buf[512] = '\xFF';
		std::ofstream out(path, std::ios::binary | std::ios::trunc);
		out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
	}

	static void write_cue_sheet(const std_fs::path& path,
	                            const std::string& bin_name)
	{
		std::ofstream out(path, std::ios::trunc);
		out << "FILE \"" << bin_name << "\" BINARY\n"
		    << "  TRACK 01 MODE1/2048\n"
		    << "    INDEX 01 00:00:00\n";
	}

	void SetUp() override
	{
		DOSBoxTestFixture::SetUp();

		// MOUNT runs the policy validators, whose globals assert
		// initialization; WEBSERVER_Init does that in production.
		// Empty config path = no image roots, interactive trust.
		init_config_dir();
		MountPolicy::InitPolicyConfig({});

		std_fs::create_directories(test_file_path);
		std_fs::create_directories(test_file_path / "plain_dir");
		std_fs::create_directories(test_file_path / "overlay_base");
		std_fs::create_directories(test_file_path / "overlay_layer");

		write_file(test_file_path / "plain_dir" / "readme.txt", 16, 'x');

		// Upstream wrote these as bare zero-filled files; the fork's
		// structure validation rejects those, so each carries the
		// minimal valid shape for its format. Sizes stay off the
		// floppy geometry table where a test expects hdd detection.

		write_iso_image(test_file_path / "image.iso");
		write_fat_image(test_file_path / "image.img", 4096);
		write_fat_image(test_file_path / "bootable.img", 65536);
		write_file(test_file_path / "raw.dat", 1440 * 1024);

		write_fat_image(test_file_path / "disk1.img", 4096);
		write_fat_image(test_file_path / "disk02.img", 4096);
		write_fat_image(test_file_path / "disk03.img", 4096);

		write_cue_sheet(test_file_path / "image.cue", "image.bin");
		write_iso_image(test_file_path / "image.bin");
		write_iso_image(test_file_path / "image.mds");
		write_iso_image(test_file_path / "image.ccd");
		write_fat_image(test_file_path / "image.ima", 4096);
		write_fat_image(test_file_path / "image.vhd", 4096);

		write_file(test_file_path / "image.flac", 2048);
		write_file(test_file_path / "image.opus", 2048);
		write_file(test_file_path / "image.ogg", 2048);
		write_file(test_file_path / "image.mp3", 2048);
		write_file(test_file_path / "image.wav", 2048);

		write_file(test_file_path / "noextfile", 2048);
	}

	void TearDown() override
	{
		DOSBoxTestFixture::TearDown();
	}

	// Runs once after all tests in this suite.
	static void TearDownTestSuite()
	{
		std::error_code ec;
		std_fs::remove_all(test_file_path, ec);
	}

	static std::string P(const std::string& name)
	{
		return (test_file_path / name).string();
	}

	static std::optional<MountParameters> Mount(const std::string& command_params)
	{
		auto cmd     = new CommandLine("Z:\\MOUNT.COM", command_params);
		auto program = new MOUNT();
		return program->ProcessArguments(cmd);
	}
};

// Use unique test file paths otherwise when when run tests in parallell
// chunks with the -j option (e.g. -j 16) the teardown and the setup steps of
// two chunks can overlap and cause test failures.
//
// Upstream anchored this to the source tree; house rule keeps test
// writes out of it. Same temp-dir shape as mount_policy_tests.cpp:
// random name plus creation check instead of mkdtemp for Windows
// portability.
std_fs::path MountTest::test_file_path = [] {
	std::random_device rd = {};
	auto dist             = std::uniform_int_distribution<uint64_t>();
	for (int attempt = 0; attempt < 16; ++attempt) {
		const auto name = std::to_string(dist(rd)) + "_mount_test_files";
		const auto candidate = std_fs::temp_directory_path() / name;
		std::error_code ec   = {};
		if (std_fs::create_directory(candidate, ec) && !ec) {
			std_fs::permissions(candidate, std_fs::perms::owner_all, ec);
			return candidate;
		}
	}
	return std_fs::path{};
}();

// ---------------------------------------------------------------------
// Error paths: ProcessArguments() must return std::nullopt.
// ---------------------------------------------------------------------

TEST_F(MountTest, RejectsUnknownType)
{
	const auto result = Mount("C " + P("plain_dir") + " -t bogus");
	EXPECT_FALSE(result.has_value());
}

TEST_F(MountTest, RejectsInvalidChsFormat)
{
	const auto result = Mount("C " + P("bootable.img") + " -t hdd -chs notnumbers");
	EXPECT_FALSE(result.has_value());
}

TEST_F(MountTest, RejectsMissingPath)
{
	const auto result = Mount("C");
	EXPECT_FALSE(result.has_value());
}

TEST_F(MountTest, RejectsDriveTokenTooLong)
{
	const auto result = Mount("WWW " + P("plain_dir"));
	EXPECT_FALSE(result.has_value());
}

TEST_F(MountTest, RejectsSecondCharNotColon)
{
	const auto result = Mount("WQ " + P("plain_dir"));
	EXPECT_FALSE(result.has_value());
}

TEST_F(MountTest, RejectsOutOfRangeDriveNumber)
{
	// Only digits '0'-'3' are valid drive numbers.
	const auto result = Mount("4 " + P("bootable.img"));
	EXPECT_FALSE(result.has_value());
}

TEST_F(MountTest, RejectsBootableLetterOutsideAtoD)
{
	// -fs none forces the A-D -> 0-3 remap in ParseDrive; the switch's
	// default case rejects any other letter.
	const auto result = Mount("E " + P("bootable.img") + " -fs none");
	EXPECT_FALSE(result.has_value());
}

TEST_F(MountTest, RejectsNonexistentPath)
{
	// Not a directory or regular file -> PROGRAM_MOUNT_ERROR_2.
	const auto result = Mount("G " + P("does_not_exist_at_all"));
	EXPECT_FALSE(result.has_value());
}

TEST_F(MountTest, RejectsOverlayWithoutMountedBase)
{
	const auto result = Mount("E " + P("overlay_layer") + " -t overlay");
	EXPECT_FALSE(result.has_value());
}

TEST_F(MountTest, NonexistentPathDoesNotConsultFailedStatMode)
{
	// The dir branch must gate on stat_ok, not on st_mode surviving a
	// failed stat via zero-initialization.
	const auto result = Mount("C " + P("definitely-not-here"));
	EXPECT_FALSE(result.has_value());
}

TEST_F(MountTest, WildcardMatchingNothingIsRejectedByImageValidation)
{
	// Upstream falls back to the literal glob string and leaves
	// openability to MountImage(). The fork validates image paths
	// before mounting, so the unresolvable literal is denied there.
	const auto result = Mount("F " + P("nomatch_*.img") + " -t floppy");

	EXPECT_FALSE(result.has_value());
}

TEST_F(MountTest, RejectsAlreadyMountedDrive)
{
	// Mounts and re-mounts the same letter within one test.
	const auto first = Mount("J " + P("plain_dir"));
	ASSERT_TRUE(first.has_value());

	const auto second = Mount("J " + P("overlay_layer"));
	EXPECT_FALSE(second.has_value());
}

// ---------------------------------------------------------------------
// Directory / overlay mounts (MountLocal() path).
//
// Note `params.paths` is NOT populated for this branch (it's only used for
// image mounts) so these tests check the fields MountLocal() itself consumes
// or mutates (drive, type, sizes, roflag, label, mediaid).
// ---------------------------------------------------------------------

TEST_F(MountTest, MountsPlainDirectoryWithDefaults)
{
	const auto result = Mount("L " + P("plain_dir"));

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->drive, 'L');
	EXPECT_EQ(result->type, MountType::Directory);
	EXPECT_FALSE(result->is_drive_number);
	EXPECT_FALSE(result->roflag);

	// Default "dir" geometry from ParseGeometry
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);
	EXPECT_EQ(result->sizes[0], 512);
	EXPECT_EQ(result->sizes[1], 32);
	EXPECT_EQ(result->sizes[2], 32765);
	EXPECT_EQ(result->sizes[3], 16000);

	// MountLocal mutates params.label to "<drive>_DRIVE" when none given
	EXPECT_EQ(result->label, "L_DRIVE");
}

TEST_F(MountTest, DirectoryMountOnDriveA_UsesFloppyMediaId)
{
	// ParseGeometry special-cases drive A/B for dir/overlay mounts.
	const auto result = Mount("A " + P("plain_dir"));

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->mediaid, MediaId::Floppy1_44MB);
}

TEST_F(MountTest, DirectoryMountReadOnly)
{
	const auto result = Mount("M " + P("plain_dir") + " -ro");

	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(result->roflag);
}

TEST_F(MountTest, DirectoryMountWithExplicitLabelIsNotOverwritten)
{
	const auto result = Mount("N " + P("plain_dir") + " -label MYLABEL");

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->label, "MYLABEL");
}

TEST_F(MountTest, OverlayMountsOnTopOfExistingDrive)
{
	const auto base = Mount("O " + P("overlay_base"));
	ASSERT_TRUE(base.has_value());

	const auto overlay = Mount("O " + P("overlay_layer") + " -t overlay");

	ASSERT_TRUE(overlay.has_value());
	EXPECT_EQ(overlay->type, MountType::Overlay);
	EXPECT_EQ(overlay->drive, 'O');
	EXPECT_EQ(overlay->label, "O_DRIVE");
}

TEST_F(MountTest, MountedImagePathIsTheValidatedCanonicalPath)
{
	// aug-fuay: the object opened must be the object validated.
	// simplify_path picks the shortest equivalent form, which for a
	// path under the cwd is the relative one; that string was then
	// re-resolved against the cwd at every later open.
	const auto old_cwd = std_fs::current_path();
	std_fs::current_path(test_file_path);
	const auto result = Mount("B raw.dat -t floppy");
	std_fs::current_path(old_cwd);

	ASSERT_TRUE(result.has_value());
	ASSERT_EQ(result->paths.size(), 1u);
	EXPECT_EQ(std_fs::path(result->paths[0]),
	          std_fs::canonical(test_file_path / "raw.dat"));
}

// ---------------------------------------------------------------------
// Geometry parsing (ParseGeometry)
// ---------------------------------------------------------------------

TEST_F(MountTest, FloppyDefaultsGeometryAndMediaId)
{
	// -t floppy is an "explicit image type", so this hits the image
	// branch via a regular file, but the geometry defaults come from
	// `ParseGeometry()` regardless of that branch.
	const auto result = Mount("B " + P("raw.dat") + " -t floppy");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::FloppyImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Fat16);
	EXPECT_EQ(result->mediaid, MediaId::Floppy1_44MB);

	EXPECT_EQ(result->sizes[0], 512);
	EXPECT_EQ(result->sizes[1], 1);
	EXPECT_EQ(result->sizes[2], 2880);
	EXPECT_EQ(result->sizes[3], 2880);
}

TEST_F(MountTest, IsoDefaultsGeometryAndFstype)
{
	const auto result = Mount("P " + P("image.iso") + " -t iso");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::CdRomImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Iso);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);

	EXPECT_EQ(result->sizes[0], 2048);
	EXPECT_EQ(result->sizes[1], 1);
	EXPECT_EQ(result->sizes[2], 65535);
	EXPECT_EQ(result->sizes[3], 0);
}

TEST_F(MountTest, ExplicitSizeOverridesDefaults)
{
	const auto result = Mount("1 " + P("bootable.img") +
	                          " -t hdd -size 512,63,16,100");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::HardDiskImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::None);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);

	EXPECT_EQ(result->sizes[0], 512);
	EXPECT_EQ(result->sizes[1], 63);
	EXPECT_EQ(result->sizes[2], 16);
	EXPECT_EQ(result->sizes[3], 100);
}

TEST_F(MountTest, ExplicitChsOverridesExplicitSize)
{
	// -chs is parsed after -size in ParseGeometry, so it should win.
	const auto result = Mount("2 " + P("bootable.img") +
	                          " -t hdd -size 512,63,16,50 -chs 200,16,63");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::HardDiskImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::None);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);

	EXPECT_EQ(result->sizes[0], 512);
	EXPECT_EQ(result->sizes[1], 63);  // sectors
	EXPECT_EQ(result->sizes[2], 16);  // heads
	EXPECT_EQ(result->sizes[3], 200); // cylinders
}

TEST_F(MountTest, FreesizeOverridesDirDefaults)
{
	const auto result = Mount("Q " + P("plain_dir") + " -freesize 100");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::Directory);
	EXPECT_EQ(result->fstype, MountFileSystemType::Fat16);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);

	// total_size_cyl stays 32765 (100MB free is under the ~250MB
	// implied default); free_size_cyl = 100*1024*1024/(512*32) = 6400.
	EXPECT_EQ(result->sizes[0], 512);
	EXPECT_EQ(result->sizes[1], 32);
	EXPECT_EQ(result->sizes[2], 32765);
	EXPECT_EQ(result->sizes[3], 6400);
}

TEST_F(MountTest, FreesizeForFloppyIsInKbNotMb)
{
	const auto result = Mount("D " + P("raw.dat") +
	                          " -t floppy -fs none -freesize 720");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::FloppyImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::None);
	EXPECT_EQ(result->mediaid, MediaId::Floppy1_44MB);

	EXPECT_EQ(result->sizes[0], 512);
	EXPECT_EQ(result->sizes[1], 1);
	EXPECT_EQ(result->sizes[2], 2880);
	EXPECT_EQ(result->sizes[3], 720 * 1024 / 512);
}

// ---------------------------------------------------------------------
// Drive parsing (ParseDrive)
// ---------------------------------------------------------------------

TEST_F(MountTest, DriveNumberForcesNoneFstypeWhenNotExplicit)
{
	const auto result = Mount("0 " + P("bootable.img"));

	ASSERT_TRUE(result.has_value());

	EXPECT_TRUE(result->is_drive_number);
	EXPECT_EQ(result->drive, '0');
	EXPECT_EQ(result->fstype, MountFileSystemType::None);
}

TEST_F(MountTest, LetterAtoDRemapsToDriveNumberWithFsNone)
{
	const auto result = Mount("C " + P("bootable.img") + " -fs none");

	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(result->is_drive_number);
	EXPECT_EQ(result->drive, '2'); // C -> drive number 2
}

// ---------------------------------------------------------------------
// -t flag aliasing
// ---------------------------------------------------------------------

TEST_F(MountTest, CdromAliasesToIso)
{
	const auto result = Mount("R " + P("image.iso") + " -t cdrom");

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->type, MountType::CdRomImage);
}

TEST_F(MountTest, FddAliasesToFloppy)
{
	const auto result = Mount("B " + P("raw.dat") + " -t fdd");

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->type, MountType::FloppyImage);
}

// ---------------------------------------------------------------------
// -ide flag
// ---------------------------------------------------------------------

TEST_F(MountTest, IdeFlagSetWithoutInvokingCableSlotLookupForNonIsoType)
{
	const auto result = Mount("3 " + P("bootable.img") +
	                          " -t hdd -size 512,63,16,100 -ide");

	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(result->is_ide);
	// ide_index/is_second_cable_slot are only touched by
	// IDE_Get_Next_Cable_Slot, which the source only calls for -t iso.
	EXPECT_EQ(result->ide_index, -1);
	EXPECT_FALSE(result->is_second_cable_slot);
}

// ---------------------------------------------------------------------
// Image-mode path collection (ProcessPaths)
// ---------------------------------------------------------------------

TEST_F(MountTest, ImplicitImageModeAutoDetectsIsoFromExtension)
{
	// No -t given; a plain existing regular file still triggers image
	// mode, and the ".iso" extension auto-sets type+fstype.
	const auto result = Mount("S " + P("image.iso"));

	ASSERT_TRUE(result.has_value());

	ASSERT_EQ(result->paths.size(), 1);
	EXPECT_EQ(result->type, MountType::CdRomImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Iso);
}

TEST_F(MountTest, AutoDetectsHddTypeFromImgExtension)
{
	const auto result = Mount("U " + P("image.img"));

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->type, MountType::HardDiskImage);
}

TEST_F(MountTest, ExplicitTypeOverridesExtensionAutoDetection)
{
	const auto result = Mount("V " + P("image.img") + " -t iso");

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->type, MountType::CdRomImage);
}

TEST_F(MountTest, MultipleExplicitPathsArePreservedInOrder)
{
	const auto result = Mount("W " + P("disk03.img") + " " + P("disk1.img") +
	                          " " + P("disk02.img") + " -t floppy");

	ASSERT_TRUE(result.has_value());

	ASSERT_EQ(result->paths.size(), 3);
	EXPECT_NE(result->paths[0].find("disk03.img"), std::string::npos);
	EXPECT_NE(result->paths[1].find("disk1.img"), std::string::npos);
	EXPECT_NE(result->paths[2].find("disk02.img"), std::string::npos);
}

TEST_F(MountTest, WildcardExpandsToMatchingFileSetUsingNaturalSort)
{
	const auto result = Mount("K " + P("disk*.img") + " -t floppy");

	ASSERT_TRUE(result.has_value());
	ASSERT_EQ(result->paths.size(), 3);

	for (const auto* expected : {"disk1.img", "disk02.img", "disk03.img"}) {
		const bool found = std::any_of(result->paths.begin(),
		                               result->paths.end(),
		                               [&](const std::string& p) {
			                               return p.find(expected) !=
			                                      std::string::npos;
		                               });
		EXPECT_TRUE(found) << "missing " << expected;
	}
}

TEST_F(MountTest, FloppyMediaIdSetWhenTypeFloppyAndFstypeFat)
{
	const auto result = Mount("H " + P("raw.dat") + " -t floppy -fs fat");

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->mediaid, MediaId::Floppy1_44MB);
}

// ---------------------------------------------------------------------
// Various edge cases
// ---------------------------------------------------------------------

TEST_F(MountTest, DirectoryOverridesExplicitFloppyTypeAndSetsFloppyLabel)
{
	const auto result = Mount("T " + P("plain_dir") + " -t floppy");

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->type, MountType::FloppyImage);
	EXPECT_EQ(result->label, "T_FLOPPY");
}

TEST_F(MountTest,
       RawHddDriveNumberMissingGeometrySucceedsAtParseLayerDespiteInternalFailure)
{
	// MountImageRaw() internally detects the missing geometry and
	// returns false, but ProcessPaths() ignores that return value on
	// every image-mount branch, so ProcessArguments() still returns a
	// populated MountParameters.
	const auto result = Mount("2 " + P("raw.dat") + " -t hdd -fs none");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->sizes[0], 0);
	EXPECT_EQ(result->sizes[1], 0);
	EXPECT_EQ(result->sizes[2], 0);
	EXPECT_EQ(result->sizes[3], 0);
}

TEST_F(MountTest, DosDrivePathAsSourceIsRejected)
{
	// Upstream resolves a DOS drive source ("J I:\") through
	// GetDosMappedHostPath's stat fallback and mounts it. The fork's
	// directory validation sees the raw DOS string, which does not
	// resolve on the host, and refuses (aug-ep2a tracks mapping the
	// resolved host path through validation instead).
	const auto base = Mount("I " + P("plain_dir"));
	ASSERT_TRUE(base.has_value());

	const auto via_dos_path = Mount("J I:\\");
	EXPECT_FALSE(via_dos_path.has_value());
}

TEST_F(MountTest, IdeFlagAsStringValueAlsoSetsIsIde)
{
	const auto result = Mount("3 " + P("bootable.img") +
	                          " -t hdd -size 512,63,16,100 -ide 1");
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(result->is_ide);
}

// ---------------------------------------------------------------------
// Extension-based auto-detection when no -t is given
// ---------------------------------------------------------------------

TEST_F(MountTest, AutoDetectsIsoFromCueExtension)
{
	const auto result = Mount("E " + P("image.cue"));

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->type, MountType::CdRomImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Iso);
}

TEST_F(MountTest, AutoDetectsIsoFromBinExtension)
{
	const auto result = Mount("E " + P("image.bin"));

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->type, MountType::CdRomImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Iso);
}

TEST_F(MountTest, AutoDetectsIsoFromMdsExtension)
{
	const auto result = Mount("E " + P("image.mds"));

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->type, MountType::CdRomImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Iso);
}

TEST_F(MountTest, AutoDetectsIsoFromCcdExtension)
{
	const auto result = Mount("E " + P("image.ccd"));

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->type, MountType::CdRomImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Iso);
}

TEST_F(MountTest, AutoDetectsHddFromImaExtension)
{
	const auto result = Mount("E " + P("image.ima"));

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->type, MountType::HardDiskImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Fat16);
}

TEST_F(MountTest, AutoDetectsHddFromVhdExtension)
{
	const auto result = Mount("F " + P("image.vhd"));

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->type, MountType::HardDiskImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Fat16);
}

// ---------------------------------------------------------------------
// Geometry-based floppy detection (#4882, kept through the #4999 port;
// aug-55a2 makes its survival mechanical instead of hand-checked)
// ---------------------------------------------------------------------

TEST_F(MountTest, EveryGeometryTableSizeDetectsAsFloppy)
{
	// Pins detection and validation to the same size set: a
	// signature-less image at any BIOS geometry table size must both
	// pass structure validation and detect as a floppy.
	for (const auto& geo : BIOS_GetDiskGeometryList()) {
		const auto img = test_file_path /
		                 (std::to_string(geo.ksize) + "k.img");
		write_file(img, static_cast<size_t>(geo.ksize) * 1024);

		const auto result = Mount("A " + img.string());
		ASSERT_TRUE(result.has_value())
		        << geo.ksize << "K image refused";
		EXPECT_EQ(result->type, MountType::FloppyImage)
		        << geo.ksize << "K image not detected as floppy";
	}
}

TEST_F(MountTest, PreDos2FloppySizeDetectsAsFloppy)
{
	// 160K SS8: signature-less self-booting era, the b1c4e3836 case.
	const auto img = test_file_path / "ss8.img";
	write_file(img, 163840);

	const auto result = Mount("A " + img.string());
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->type, MountType::FloppyImage);
}

TEST_F(MountTest, DskExtensionUsesGeometryDetection)
{
	const auto img = test_file_path / "disk.dsk";
	write_file(img, 737280);

	const auto result = Mount("A " + img.string());
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->type, MountType::FloppyImage);
}

TEST_F(MountTest, GeometryMatchHasKilobyteGranularity)
{
	// Size matching truncates to whole KB (st_size / 1024), so one
	// extra sector past 160K still reads as the 160K geometry.
	const auto ragged = test_file_path / "ragged160.img";
	write_fat_image(ragged, 163840 + 512);

	const auto ragged_result = Mount("A " + ragged.string());
	ASSERT_TRUE(ragged_result.has_value());
	EXPECT_EQ(ragged_result->type, MountType::FloppyImage);

	// A full KB off the table falls through to hdd; the FAT
	// signature carries it past structure validation.
	const auto off = test_file_path / "off161.img";
	write_fat_image(off, 163840 + 1024);

	const auto off_result = Mount("C " + off.string());
	ASSERT_TRUE(off_result.has_value());
	EXPECT_EQ(off_result->type, MountType::HardDiskImage);
}

// ---------------------------------------------------------------------
// Autosize: the #4999 motivating bug at unit level (aug-vc54)
// ---------------------------------------------------------------------

TEST_F(MountTest, AutosizeDerivesGeometryForDetectedHddImage)
{
	// End of the aug-vc54 chain: a signed hdd image mounted without
	// -t detects as hdd, geometry defaulting does not poison it, and
	// autosize derives cylinders from the file size (16 heads x 63
	// spt fixed, per MountImageFat).
	const auto img = test_file_path / "auto.img";
	write_fat_image(img, 16 * 63 * 512);

	const auto result = Mount("C " + img.string());
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->type, MountType::HardDiskImage);
	EXPECT_EQ(result->sizes[0], 512);
	EXPECT_EQ(result->sizes[1], 63);
	EXPECT_EQ(result->sizes[2], 16);
	EXPECT_EQ(result->sizes[3], 1);
}

TEST_F(MountTest, AutosizeLeavesSizesZeroForNonCylinderMultiple)
{
	// Half a cylinder extra: autosize's exact-multiple gate refuses
	// and the sizes stay untouched.
	const auto img = test_file_path / "ragged.img";
	write_fat_image(img, 16 * 63 * 512 + 512);

	const auto result = Mount("C " + img.string());
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->sizes[0], 0);
	EXPECT_EQ(result->sizes[1], 0);
	EXPECT_EQ(result->sizes[2], 0);
	EXPECT_EQ(result->sizes[3], 0);
}

// ---------------------------------------------------------------------
// Policy seam through MountPaths
// ---------------------------------------------------------------------

TEST_F(MountTest, LockedMountIsRefused)
{
	// The lock is one-way per process; ctest isolates each test in
	// its own process, and an in-process full run gets the skip.
	if (MountPolicy::IsLocked()) {
		GTEST_SKIP() << "Lock already set by a prior test";
	}

	const auto before = Mount("I " + P("plain_dir"));
	ASSERT_TRUE(before.has_value());

	MountPolicy::Lock();

	const auto after = Mount("K " + P("plain_dir"));
	EXPECT_FALSE(after.has_value());
}

// ---------------------------------------------------------------------
// Known-crashing combinations
// ---------------------------------------------------------------------

/* TODO this test only passes in debug mode; fix this at some point

TEST_F(MountTest, DriveNumberWithExplicitFatFstypeCrashesOnDriveIndex)
{
	// MountImageFat() calls drive_index() on params.drive, which is a
	// digit character ('1') when is_drive_number is true and -fs fat
	// is given explicitly. drive_index() asserts drive_letter is 'A'-'Z',
	// so this combination currently aborts the process. Pinning that
	// behaviour rather than hiding it.
	EXPECT_DEATH(Mount("1 " + P("bootable.img") +
	                   " -fs fat -t hdd -size 512,63,16,100"),
	             "drive_letter");
}
*/

// ---------------------------------------------------------------------
// Option precedence
// ---------------------------------------------------------------------

TEST_F(MountTest, ExplicitSizeOverridesFreesize)
{
	const auto result = Mount("X " + P("plain_dir") +
	                          " -freesize 100 -size 512,63,16,42");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::Directory);
	EXPECT_EQ(result->fstype, MountFileSystemType::Fat16);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);

	EXPECT_EQ(result->sizes[0], 512);
	EXPECT_EQ(result->sizes[1], 63);
	EXPECT_EQ(result->sizes[2], 16);
	EXPECT_EQ(result->sizes[3], 42);
}

TEST_F(MountTest, ChsOverridesFreesize)
{
	const auto result = Mount("X " + P("plain_dir") +
	                          " -freesize 100 -chs 200,16,63");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::Directory);
	EXPECT_EQ(result->fstype, MountFileSystemType::Fat16);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);

	EXPECT_EQ(result->sizes[0], 512);
	EXPECT_EQ(result->sizes[1], 63);
	EXPECT_EQ(result->sizes[2], 16);
	EXPECT_EQ(result->sizes[3], 200);
}

TEST_F(MountTest, ChsOverridesSizeRegardlessOfArgumentOrder)
{
	const auto result = Mount("1 " + P("bootable.img") +
	                          " -chs 200,16,63 -size 512,63,16,50 -t hdd");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::HardDiskImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::None);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);

	EXPECT_EQ(result->sizes[0], 512);
	EXPECT_EQ(result->sizes[1], 63);
	EXPECT_EQ(result->sizes[2], 16);
	EXPECT_EQ(result->sizes[3], 200);
}

// ---------------------------------------------------------------------
// Auto-detection precedence
// ---------------------------------------------------------------------

TEST_F(MountTest, ExplicitIsoTypeOverridesImgExtension)
{
	const auto result = Mount("X " + P("image.img") + " -t iso");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::CdRomImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Iso);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);

	EXPECT_EQ(result->sizes[0], 2048);
	EXPECT_EQ(result->sizes[1], 1);
	EXPECT_EQ(result->sizes[2], 65535);
	EXPECT_EQ(result->sizes[3], 0);
}

TEST_F(MountTest, ExplicitHddTypeKeepsFatFilesystem)
{
	const auto result = Mount("X " + P("image.iso") +
	                          " -t hdd -size 512,63,16,100");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::HardDiskImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Fat16);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);

	EXPECT_EQ(result->sizes[0], 512);
	EXPECT_EQ(result->sizes[1], 63);
	EXPECT_EQ(result->sizes[2], 16);
	EXPECT_EQ(result->sizes[3], 100);
}

// ---------------------------------------------------------------------
// Explicit -fs interactions
// ---------------------------------------------------------------------

TEST_F(MountTest, IsoExtensionOverridesExplicitFatFs)
{
	const auto result = Mount("X " + P("image.iso") + " -fs fat");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::CdRomImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Iso);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);
}

TEST_F(MountTest, IsoTypeRejectsNoneFilesystem)
{
	const auto result = Mount("X " + P("image.iso") + " -t iso -fs none");

	EXPECT_FALSE(result.has_value());
}

TEST_F(MountTest, ExplicitIsoFsForFloppyTypeIsRejected)
{
	// Resolves the upstream author's own TODO: a floppy-typed mount
	// cannot carry an ISO filesystem (both maintainers agree, aug-55a2).
	const auto result = Mount("A " + P("raw.dat") + " -t floppy -fs iso");

	EXPECT_FALSE(result.has_value());
}

TEST_F(MountTest, ExplicitIsoTypeOverridesFloppyExtension)
{
	const auto result = Mount("D " + P("bootable.img") + " -t iso");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::CdRomImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Iso);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);

	EXPECT_EQ(result->sizes[0], 2048);
	EXPECT_EQ(result->sizes[1], 1);
	EXPECT_EQ(result->sizes[2], 65535);
	EXPECT_EQ(result->sizes[3], 0);
}

TEST_F(MountTest, ExplicitFloppyTypeOverridesIsoExtension)
{
	const auto result = Mount("A " + P("image.iso") + " -t floppy");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::FloppyImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Fat16);

	// Pin down whatever the parser currently does.
	EXPECT_EQ(result->mediaid, MediaId::Floppy1_44MB);
}

TEST_F(MountTest, ExplicitIsoFilesystemWithoutType)
{
	const auto result = Mount("D " + P("bootable.img") + " -fs iso");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::HardDiskImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Iso);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);
}

TEST_F(MountTest, ExplicitFatFilesystemWithoutType)
{
	const auto result = Mount("D " + P("bootable.img") + " -fs fat");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::HardDiskImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Fat16);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);
}

TEST_F(MountTest, ExplicitIsoFilesystemOnIsoImage)
{
	const auto result = Mount("D " + P("image.iso") + " -fs iso");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::CdRomImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Iso);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);
}

// ---------------------------------------------------------------------
// Image geometry oddities
// ---------------------------------------------------------------------

TEST_F(MountTest, SizeAcceptedForIsoMount)
{
	const auto result = Mount("X " + P("image.iso") + " -t iso -size 512,63,16,99");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::CdRomImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Iso);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);

	EXPECT_EQ(result->sizes[0], 512);
	EXPECT_EQ(result->sizes[1], 63);
	EXPECT_EQ(result->sizes[2], 16);
	EXPECT_EQ(result->sizes[3], 99);
}

TEST_F(MountTest, ChsAcceptedForIsoMount)
{
	const auto result = Mount("X " + P("image.iso") + " -t iso -chs 123,8,17");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::CdRomImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Iso);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);

	EXPECT_EQ(result->sizes[0], 512);
	EXPECT_EQ(result->sizes[1], 17);
	EXPECT_EQ(result->sizes[2], 8);
	EXPECT_EQ(result->sizes[3], 123);
}

TEST_F(MountTest, FreesizeMutatesGeometryForImageMount)
{
	const auto result = Mount("1 " + P("bootable.img") + " -t hdd -freesize 50");

	ASSERT_TRUE(result.has_value());

	EXPECT_NE(result->sizes[3], 0);
}

// ---------------------------------------------------------------------
// Multiple image behaviour
// ---------------------------------------------------------------------

TEST_F(MountTest, FirstImageControlsAutoDetection)
{
	const auto result = Mount("X " + P("image.img") + " " + P("image.iso"));

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::HardDiskImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Fat16);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);

	ASSERT_EQ(result->paths.size(), 2);
}

TEST_F(MountTest, FirstImageControlsAutoDetectionReverseOrder)
{
	const auto result = Mount("X " + P("image.iso") + " " + P("image.img"));

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::CdRomImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Iso);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);

	ASSERT_EQ(result->paths.size(), 2);
}

TEST_F(MountTest, FirstIsoImageControlsAutoDetection)
{
	const auto result = Mount("D " + P("image.iso") + " " + P("bootable.img"));

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::CdRomImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Iso);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);

	ASSERT_EQ(result->paths.size(), 2);
}

// ---------------------------------------------------------------------
// IDE interactions
// ---------------------------------------------------------------------

TEST_F(MountTest, IdeFlagFollowedBySwitchIsRejected)
{
	// FindString("-ide", ...) consumes the next token as its value,
	// so "-ide -size N" orphans the size string into the path list.
	// Upstream mounts anyway with the -size intent silently lost;
	// the fork's path validation refuses the leftover (aug-adow).
	const auto result = Mount("D " + P("bootable.img") +
	                          " -t hdd -fs iso -ide -size 512,63,16,100");

	EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------
// Geometry precedence with ISO images
// ---------------------------------------------------------------------

TEST_F(MountTest, ExplicitSizeWithIsoImage)
{
	const auto result = Mount("D " + P("image.iso") + " -size 512,63,16,99");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::CdRomImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Iso);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);

	// Pin down whether ISO defaults or explicit size wins.
}

TEST_F(MountTest, ExplicitChsWithIsoImage)
{
	const auto result = Mount("D " + P("image.iso") + " -chs 123,8,17");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->type, MountType::CdRomImage);
	EXPECT_EQ(result->fstype, MountFileSystemType::Iso);
}

// ---------------------------------------------------------------------
// Miscellaneous parser state
// ---------------------------------------------------------------------

TEST_F(MountTest, LabelPreservedForIsoMount)
{
	const auto result = Mount("D " + P("image.iso") + " -label MYDISC");

	ASSERT_TRUE(result.has_value());

	EXPECT_EQ(result->label, "MYDISC");
}

TEST_F(MountTest, ReadOnlyPreservedForIsoMount)
{
	const auto result = Mount("D " + P("image.iso") + " -ro");

	ASSERT_TRUE(result.has_value());

	EXPECT_TRUE(result->roflag);
}

TEST_F(MountTest, ReadOnlyPreservedForDirectoryMount)
{
	const auto result = Mount("D " + P("plain_dir") + " -ro");

	ASSERT_TRUE(result.has_value());

	EXPECT_TRUE(result->roflag);
}

// ---------------------------------------------------------------------
// Duplicate option precedence
// ---------------------------------------------------------------------

// Upstream asserts first-wins for duplicate switches: parsing consumes
// only the first pair, the second leaks into the path list and upstream
// mounts regardless. The fork refuses unconsumed tokens at path
// validation, so a duplicated switch fails the whole command. First-wins
// coverage returns with the parser consumption fix (aug-adow).

TEST_F(MountTest, DuplicateLabelIsRejected)
{
	const auto result = Mount("X " + P("image.img") +
	                          " -label FIRST"
	                          " -label SECOND");

	EXPECT_FALSE(result.has_value());
}

TEST_F(MountTest, DuplicateSizeIsRejected)
{
	const auto result = Mount("X " + P("image.img") +
	                          " -size 1,2,3,4"
	                          " -size 5,6,7,8");

	EXPECT_FALSE(result.has_value());
}

TEST_F(MountTest, DuplicateChsIsRejected)
{
	const auto result = Mount("X " + P("image.img") +
	                          " -chs 100,17,8"
	                          " -chs 200,63,16");

	EXPECT_FALSE(result.has_value());
}

TEST_F(MountTest, DuplicateFilesystemIsRejected)
{
	const auto result = Mount("X " + P("image.img") +
	                          " -fs fat"
	                          " -fs iso");

	EXPECT_FALSE(result.has_value());
}

TEST_F(MountTest, DuplicateTypeIsRejected)
{
	const auto result = Mount("X " + P("image.img") +
	                          " -t floppy"
	                          " -t iso");

	EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------
// Sink invariant (aug-2yza): no drive is constructed from a host path
// without a preceding allowed verdict for that same path. A future door
// that skips the policy fails these instead of repeating aug-32ok.
// ---------------------------------------------------------------------

namespace sink {

std::vector<std::string> constructed = {};

struct RecordedVerdict {
	std::string raw      = {};
	MountVerdict verdict = {};
};
std::vector<RecordedVerdict> verdicts = {};

void CaptureConstruction(const char* host_path)
{
	constructed.emplace_back(host_path);
}

void CaptureVerdict(const std_fs::path& raw_path, const MountVerdict& verdict)
{
	verdicts.push_back({raw_path.string(), verdict});
}

struct HookCapture {
	HookCapture()
	{
		constructed.clear();
		verdicts.clear();
		MountPolicy::drive_construction_hook = &CaptureConstruction;
		MountPolicy::verdict_hook            = &CaptureVerdict;
	}
	~HookCapture()
	{
		MountPolicy::drive_construction_hook = nullptr;
		MountPolicy::verdict_hook            = nullptr;
	}
};

// localDrive's basedir carries a trailing separator; compare canonically
bool WasAllowed(const std::string& constructed_path)
{
	std::error_code ec   = {};
	const auto canonical = std_fs::canonical(constructed_path, ec);
	if (ec) {
		return false;
	}
	for (const auto& v : verdicts) {
		if (v.verdict.allowed && v.verdict.resolved == canonical) {
			return true;
		}
	}
	return false;
}

} // namespace sink

TEST_F(MountTest, EveryDriveConstructionFollowsAnAllowedVerdict)
{
	sink::HookCapture capture;

	ASSERT_TRUE(Mount("V " + P("image.img") + " -t floppy").has_value());
	ASSERT_TRUE(Mount("X " + P("image.iso")).has_value());
	ASSERT_TRUE(Mount("U " + P("plain_dir")).has_value());
	ASSERT_TRUE(Mount("S " + P("disk1.img") + " " + P("disk02.img") + " -t floppy")
	                    .has_value());
	ASSERT_TRUE(Mount("A " + P("raw.dat") + " -fs none").has_value());

	ASSERT_FALSE(sink::constructed.empty());
	for (const auto& path : sink::constructed) {
		EXPECT_TRUE(sink::WasAllowed(path))
		        << "constructed without allowed verdict: " << path;
	}
}

TEST_F(MountTest, SwapConstructsOnlyTheValidatedImage)
{
	sink::HookCapture capture;

	ASSERT_TRUE(Mount("T " + P("image.img") + " -t floppy").has_value());

	write_dos1_floppy(test_file_path / "swap_disk.img");
	const auto result = DriveSwap::Swap('T',
	                                    P("swap_disk.img"),
	                                    false,
	                                    {},
	                                    {std_fs::canonical(test_file_path)});
	ASSERT_TRUE(result.ok) << result.error;

	ASSERT_FALSE(sink::constructed.empty());
	for (const auto& path : sink::constructed) {
		EXPECT_TRUE(sink::WasAllowed(path))
		        << "constructed without allowed verdict: " << path;
	}
}

TEST_F(MountTest, InvariantCheckDetectsAnUnvalidatedConstruction)
{
	// The instrument proof: a door that skips the policy entirely must
	// be visible to the invariant check, or the check proves nothing.
	sink::HookCapture capture;

	const auto drive = std::make_shared<fatDrive>(
	        P("image.img").c_str(), 512, 0, 0, 0, 0xF0, true);
	(void)drive;

	ASSERT_FALSE(sink::constructed.empty());
	EXPECT_FALSE(sink::WasAllowed(sink::constructed.front()));
}

#if !defined(WIN32)
TEST_F(MountTest, SymlinkToDeviceIsRefusedByThePolicyItself)
{
	// The aug-cbly finding end to end: the hostile path must REACH the
	// policy and be refused there, not get filtered out earlier by luck.
	if (!std_fs::exists("/dev/zero")) {
		GTEST_SKIP() << "/dev/zero not available";
	}
	sink::HookCapture capture;

	const auto link    = test_file_path / "zero.img";
	std::error_code ec = {};
	std_fs::create_symlink("/dev/zero", link, ec);
	ASSERT_FALSE(ec) << ec.message();

	EXPECT_FALSE(Mount("Q " + link.string() + " -t floppy").has_value());

	bool policy_refused_type = false;
	for (const auto& v : sink::verdicts) {
		if (!v.verdict.allowed &&
		    v.verdict.reason == DenyReason::NotRegularFile) {
			policy_refused_type = true;
		}
	}
	EXPECT_TRUE(policy_refused_type);
	EXPECT_TRUE(sink::constructed.empty());
}
#endif

} // namespace
