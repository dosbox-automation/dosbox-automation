// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/archive/archive_management.h"
#include "dos/archive/archive_toml.h"
#include "dos/programs/mount.h"

#include "archive_fixtures.h"
#include "config/setup.h"
#include "dos/archive/archive_cache.h"
#include "dos/archive/archive_helper_client.h"
#include "dos/archive/archive_lock.h"
#include "dos/archive/archive_reader.h"
#include "dos/archive/head_sniff.h"
#include "dos/drive_swap.h"
#include "dos/drives.h"
#include "misc/support.h"
#include "dos/programs/archive_chooser.h"
#include "dos/programs/archive_images.h"
#include "dos/programs/boot.h"
#include "dos/programs/menu_session.h"
#include "dos/programs/mount_archive_internal.h"
#include "dos/programs/mount_common.h"
#include "dos/programs/mount_policy.h"
#include "dos/programs/mount_policy_system_paths.h"
#include "dosbox_test_fixture.h"
#include "hardware/dma.h"
#include "ints/bios_disk.h"
#include "misc/cross.h"
#include "shell/shell.h"
#include "test_temp_dir.h"
#include "utils/env_utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ranges>
#include <string>
#include <vector>

// The shell's copy of a program's full command line (shell_misc.cpp).
extern std::string full_arguments;

using ArchiveFixtures::FileEntry;
using ArchiveFixtures::Filter;
using ArchiveFixtures::IszChunk;
using ArchiveFixtures::Kind;
using ArchiveFixtures::MinimalIso;
using ArchiveFixtures::WriteArchive;
using ArchiveFixtures::WriteCompressed;
using ArchiveFixtures::WriteIsz;

namespace {

namespace std_fs = std::filesystem;

void SetEnv(const char* name, const char* value)
{
#if defined(WIN32)
	_putenv_s(name, value);
#else
	setenv(name, value, 1);
#endif
}

std::vector<FileEntry> Game()
{
	return {
	        {"game/", "", true},
	        {"game/doom.exe", std::string(3000, 'x')},
	        {"game/data/", "", true},
	        {"game/data/level.wad", std::string(5000, 'w')},
	        {"game/readme.txt", "hello"},
	};
}

// A 320K DOS 1.x floppy: fatDrive synthesizes the BPB from the media byte
// (two one-sector FATs, so the root directory starts at sector 3), with a
// volume label entry first in the root.
std::string FloppyBytes(const std::string& label)
{
	std::string img(327680, '\0');
	img[512]              = '\xFF';
	constexpr size_t Root = 3 * 512;
	img.replace(Root, 11, label + std::string(11 - label.size(), ' '));
	img[Root + 11] = '\x08';
	return img;
}

std::string ReadAll(const std_fs::path& path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string(std::istreambuf_iterator<char>(in), {});
}

std::vector<FileEntry> Images()
{
	return {
	        {"disks/", "", true},
	        {"disks/disk1.img", FloppyBytes("DISKONE")},
	        {"disks/disk2.img", FloppyBytes("DISKTWO")},
	        {"cd/", "", true},
	        {"cd/game.iso", MinimalIso()},
	        {"cd/track.bin", MinimalIso()},
	        {"cd/game.cue",
	         "FILE \"track.bin\" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n"},
	        {"game/", "", true},
	        {"game/readme.txt", "hello"},
	};
}

// Archives with the same content share one sidecar, so a second mount
// of the same set needs another readme.
std::vector<FileEntry> OneProgram(const std::string& readme = "hello")
{
	return {
	        {  "game.exe",   "MZ"},
	        {"readme.txt", readme},
	};
}

std::vector<FileEntry> TwoPrograms(const std::string& readme = "hello")
{
	return {
	        { "setup.exe",   "MZ"},
	        {  "game.exe",   "MZ"},
	        {"readme.txt", readme},
	};
}

// FloppyBytes with an empty X.EXE after the label in the root directory.
std::string FloppyWithProgram()
{
	auto img              = FloppyBytes("RUNDISK");
	constexpr size_t Root = 3 * 512;
	img.replace(Root + 32, 11, "X       EXE");
	img[Root + 32 + 11] = '\x20';
	return img;
}

std::vector<std::pair<std::string, MountVerdict>> verdicts = {};
std::vector<std::string> constructed                       = {};

void CaptureVerdict(const std_fs::path& raw, const MountVerdict& verdict)
{
	verdicts.emplace_back(raw.string(), verdict);
}
void CaptureConstruction(const char* host_path)
{
	constructed.emplace_back(host_path);
}

class MountArchiveTest : public DOSBoxTestFixture {
protected:
	void SetUp() override
	{
		DOSBoxTestFixture::SetUp();
		// The policy asserts its initialisation (gotcha 2026-08).
		MountPolicy::InitPolicyConfig({});
		tmp = TestTempDir::MakeUnique("mount_archive_");
		ASSERT_FALSE(tmp.empty());
		ArchiveMount::SetHelperPathOverride(DOSBOX_ARCHIVE_HELPER_PATH);
		Set("archive_cache_path", (tmp / "cache").string());
		Set("archive_sidecar_path", (tmp / "saves").string());
		MOUNT::ResetArchiveCacheForTests();
		verdicts.clear();
		constructed.clear();
		MountPolicy::verdict_hook            = &CaptureVerdict;
		MountPolicy::drive_construction_hook = &CaptureConstruction;
		program = std::make_unique<MOUNT>();
	}
	// The engine always has the bundled Y:, and the menu turn needs it.
	void MountBundledY()
	{
		previous_y = Drives.at(drive_index('Y'));
		y_replaced = true;
		Drives.at(drive_index('Y')) = std::make_shared<localDrive>(
		        (get_resource_path("drives", "y").string() + CROSS_FILESPLIT).c_str(),
		        512, 32, 32765, 16000, 0xf8, false, true);
	}
	void TearDown() override
	{
		if (y_replaced) {
			Drives.at(drive_index('Y')) = previous_y;
		}
		for (char d : {'A', 'D', 'E', 'F'}) {
			if (Drives.at(drive_index(d))) {
				UnmountHelper(d);
			}
		}
		SHELL_TakePendingCommands();
		ArchiveImages::ReleaseAllForTests();
		imageDiskList.fill(nullptr);
		// BOOT touches the DMA controller; its static would otherwise
		// be destroyed at exit after the logger it reports into
		// (2026-10-02).
		DMA_Destroy();
		MountPolicy::verdict_hook            = nullptr;
		MountPolicy::drive_construction_hook = nullptr;
		ArchiveMount::SetHelperPathOverride({});
		MOUNT::ResetArchiveCacheForTests();
		program.reset();
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
	std_fs::path Built(const std::string& name, const Kind kind,
	                   const std::vector<FileEntry>& entries = Game())
	{
		const auto path = tmp / name;
		auto error      = std::string();
		EXPECT_TRUE(WriteArchive(path, kind, entries, nullptr, &error))
		        << error;
		return path;
	}
	// augra-pack's shape: info.toml beside one stored inner zip named
	// after the outer with ".store" (augra-pack deflates the toml, which
	// changes nothing here). The inner file stays in tmp, never mounted.
	std_fs::path House(const std::string& name,
	                   const std::vector<FileEntry>& payload = Game(),
	                   const std::string& toml = "[game]\nname = \"x\"\n",
	                   const Kind inner_kind   = Kind::ZipStored)
	{
		const auto stem = std_fs::path(name).stem().string();
		const auto inner = Built(stem + ".store.zip", inner_kind, payload);
		return Built(name,
		             Kind::ZipStored,
		             {
		                     {        "info.toml",           toml},
		                     {stem + ".store.zip", ReadAll(inner)}
                });
	}
	// A stored outer around a built inner, plus a notes file so the outer
	// is a wild archive (never a house member): the drive must come from
	// the spelled chain, not from the rule. Game() has its own readme.
	std_fs::path Outer(const std::string& name, const std::string& inner_name,
	                   const Kind inner_kind = Kind::ZipStored,
	                   const Kind outer_kind = Kind::ZipStored,
	                   const std::vector<FileEntry>& payload = Game())
	{
		const auto inner = Built(inner_name, inner_kind, payload);
		return Built(name,
		             outer_kind,
		             {
		                     {"notes.txt", "a collection"},
		                     { inner_name, ReadAll(inner)},
                });
	}
	// Members copied into the cache: regular files under any entry's
	// files directory. A lease alone records an entry and copies nothing.
	static int CacheCopies(const std_fs::path& cache)
	{
		auto copies        = 0;
		std::error_code ec = {};
		if (!std_fs::is_directory(cache, ec)) {
			return 0;
		}
		for (const auto& item :
		     std_fs::recursive_directory_iterator(cache, ec)) {
			if (item.is_regular_file(ec) &&
			    item.path().parent_path().filename() ==
			            ArchiveMount::FilesDirName) {
				++copies;
			}
		}
		return copies;
	}
	// ~Program deletes cmd, so the line lives on the heap and the old
	// one goes before the next (the mount tests leak theirs instead).
	CommandLine* SetCommand(const std::string& command_params)
	{
		delete program->cmd;
		program->cmd = new CommandLine("MOUNT", command_params);
		return program->cmd;
	}
	std::optional<MountParameters> Mount(const std::string& command_params)
	{
		return program->ProcessArguments(SetCommand(command_params));
	}
	// The archive drive, behind its union when the mount has a sidecar.
	static std::shared_ptr<ArchiveDrive> DriveAt(const char letter)
	{
		return ArchiveDriveOf(Drives.at(drive_index(letter)));
	}
	static std::shared_ptr<UnionDrive> UnionAt(const char letter)
	{
		return std::dynamic_pointer_cast<UnionDrive>(
		        Drives.at(drive_index(letter)));
	}

	// A cache entry of a mounted and unmounted tar.gz, aged past the TTL.
	std_fs::path ExpiredCacheEntry()
	{
		Set("archive_sequential", "cache");
		const auto tgz = Built("doom.tar.gz", Kind::TarGz);
		EXPECT_TRUE(Mount("D " + tgz.string()).has_value());
		const auto entry = ArchiveMount::ArchiveCache::EntryDirectory(
		        tmp / "cache",
		        "doom.tar",
		        DriveAt('D')->Source().IdentityHash());
		UnmountHelper('D');
		EXPECT_TRUE(std_fs::is_regular_file(
		        entry / ArchiveMount::FilesDirName / "1"));
		auto error = std::string();
		auto doc = ArchiveToml::ReadFile(entry / ArchiveMount::EntryTomlName,
		                                 &error);
		EXPECT_TRUE(doc) << error;
		if (doc) {
			doc->root.Set("last_use", int64_t{1});
			EXPECT_TRUE(ArchiveToml::WriteAtomically(
			        entry / ArchiveMount::EntryTomlName, *doc, &error))
			        << error;
		}
		return entry;
	}

	std_fs::path tmp               = {};
	std::unique_ptr<MOUNT> program = {};
	std::shared_ptr<DOS_Drive> previous_y = {};
	bool y_replaced                       = false;
};

TEST_F(MountArchiveTest, MountsAZipFoundByExtensionAndMagic)
{
	const auto zip    = Built("doom.zip", Kind::ZipStored);
	const auto result = Mount("D " + zip.string());
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->type, MountType::Archive);
	EXPECT_FALSE(result->is_image_mode);
	EXPECT_EQ(result->mediaid, MediaId::HardDisk);
	auto drive = DriveAt('D');
	ASSERT_TRUE(drive);
	// The message stubs of dosbox_tests give every MSG_Get an empty
	// string, so the type is checked by its enum, not its text.
	EXPECT_EQ(drive->GetType(), DosDriveType::Archive);
	EXPECT_EQ(std::string(drive->GetInfo()), std_fs::canonical(zip).string());
	EXPECT_STREQ(drive->GetLabel(), "DOOM");
	// D10: the one top directory became the root.
	EXPECT_TRUE(drive->TestDir("DATA"));
	EXPECT_FALSE(drive->TestDir("GAME"));
	EXPECT_TRUE(drive->FileExists("README.TXT"));
	// The sink invariant: a verdict for the path before the construction.
	ASSERT_EQ(verdicts.size(), 1u);
	EXPECT_TRUE(verdicts[0].second.allowed);
	// Two constructions: the archive, then the sidecar's local drive.
	ASSERT_EQ(constructed.size(), 2u);
	EXPECT_EQ(constructed[0], std_fs::canonical(zip).string());
	EXPECT_TRUE(constructed[1].starts_with((tmp / "saves").string()))
	        << constructed[1];
	EXPECT_EQ(ArchiveMount::ArchiveHelperClient::LiveHelpers(), 1);
	// The cache got its entry and the tag.
	EXPECT_TRUE(std_fs::is_regular_file(tmp / "cache" /
	                                    ArchiveMount::CacheTagName));
	// Unmount ends the helper (once nothing else holds the drive).
	drive.reset();
	UnmountHelper('D');
	EXPECT_FALSE(Drives.at(drive_index('D')));
	EXPECT_EQ(ArchiveMount::ArchiveHelperClient::LiveHelpers(), 0);
}

TEST_F(MountArchiveTest, ExplicitTypeArchiveAndTheRootOption)
{
	const auto zip = Built("doom.zip", Kind::ZipStored);
	auto result = Mount("D " + zip.string() + " -t archive -root game\\data");
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->archive_root, "GAME\\DATA");
	auto drive = DriveAt('D');
	ASSERT_TRUE(drive);
	EXPECT_TRUE(drive->FileExists("LEVEL.WAD"));
	EXPECT_FALSE(drive->TestDir("DATA"));
	drive.reset();
	UnmountHelper('D');
	// A root that is not in the archive refuses the mount, after the
	// helper was started and before any drive exists.
	EXPECT_FALSE(Mount("D " + zip.string() + " -root NOPE").has_value());
	EXPECT_FALSE(Drives.at(drive_index('D')));
	EXPECT_EQ(ArchiveMount::ArchiveHelperClient::LiveHelpers(), 0);
	// Forward slashes and lower case are accepted.
	result = Mount("E " + zip.string() + " -root game/data/");
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->archive_root, "GAME\\DATA");
}

TEST_F(MountArchiveTest, AHouseArchiveMountsAsItsGame)
{
	const auto house = House("doom.zip");
	auto result      = Mount("D " + house.string());
	ASSERT_TRUE(result.has_value());
	auto drive = DriveAt('D');
	ASSERT_TRUE(drive);
	// D10 inside the entered archive: Game() lives under game/.
	EXPECT_TRUE(drive->FileExists("DOOM.EXE"));
	EXPECT_TRUE(drive->TestDir("DATA"));
	EXPECT_FALSE(drive->FileExists("INFO.TOML"));
	EXPECT_FALSE(drive->FileExists("DOOM~1.ZIP"));
	EXPECT_FALSE(drive->FileExists("DOOM.STORE.ZIP"));
	// The info text names the host and the member.
	EXPECT_NE(drive->GetInfoString().find("doom.store.zip"), std::string::npos)
	        << drive->GetInfoString();
	// The entered archive is read in place: nothing was copied.
	EXPECT_EQ(CacheCopies(tmp / "cache"), 0);
	drive.reset();
	UnmountHelper('D');
	EXPECT_EQ(ArchiveMount::ArchiveHelperClient::LiveHelpers(), 0);
}

TEST_F(MountArchiveTest, AHouseArchiveWithADeflatedMemberEntersThroughTheCache)
{
	const auto inner = Built("doom.store.zip", Kind::ZipStored, Game());
	const auto house = Built("doom.zip",
	                         Kind::ZipDeflate,
	                         {
	                                 {     "info.toml",     "[game]\n"},
	                                 {"doom.store.zip", ReadAll(inner)}
        });
	auto result      = Mount("D " + house.string());
	ASSERT_TRUE(result.has_value());
	ASSERT_TRUE(DriveAt('D'));
	EXPECT_TRUE(DriveAt('D')->FileExists("DOOM.EXE"));
	// One copy in the cache: the inner zip.
	EXPECT_EQ(CacheCopies(tmp / "cache"), 1);
}

TEST_F(MountArchiveTest, TheNearMissesStayFilesOnTheDrive)
{
	const auto inner = Built("game.zip", Kind::ZipStored, Game());
	const auto wild  = Built("wild.zip",
                                Kind::ZipStored,
	                         {
                                        {"readme.txt",        "hello"},
                                        {  "game.zip", ReadAll(inner)}
        });
	ASSERT_TRUE(Mount("D " + wild.string()).has_value());
	ASSERT_TRUE(DriveAt('D'));
	EXPECT_TRUE(DriveAt('D')->FileExists("README.TXT"));
	EXPECT_TRUE(DriveAt('D')->FileExists("GAME.ZIP"));
	EXPECT_FALSE(DriveAt('D')->FileExists("DOOM.EXE"));
	UnmountHelper('D');
	const auto two = Built("two.zip",
	                       Kind::ZipStored,
	                       {
	                               {"a.zip", ReadAll(inner)},
	                               {"b.zip", ReadAll(inner)}
        });
	ASSERT_TRUE(Mount("E " + two.string()).has_value());
	ASSERT_TRUE(DriveAt('E'));
	EXPECT_TRUE(DriveAt('E')->FileExists("A.ZIP"));
	EXPECT_TRUE(DriveAt('E')->FileExists("B.ZIP"));
}

TEST_F(MountArchiveTest, RootAppliesInsideTheEnteredArchive)
{
	const auto house = House("doom.zip");
	auto result      = Mount("D " + house.string() + " -root game/data");
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->archive_root, "GAME\\DATA");
	ASSERT_TRUE(DriveAt('D'));
	EXPECT_TRUE(DriveAt('D')->FileExists("LEVEL.WAD"));
	UnmountHelper('D');
	EXPECT_FALSE(Mount("E " + house.string() + " -root nope").has_value());
	EXPECT_EQ(ArchiveMount::ArchiveHelperClient::LiveHelpers(), 0);
}

TEST_F(MountArchiveTest, AnEnteredDrivesSidecarCarriesTheLeafIdentityUnderTheHostsName)
{
	const auto house = House("doom.zip");
	ASSERT_TRUE(Mount("D " + house.string()).has_value());
	auto drive = UnionAt('D');
	ASSERT_TRUE(drive);
	const auto leaf    = ArchiveDriveOf(drive)->Source().IdentityHash();
	const auto as_file = ArchiveMount::ArchiveReader::Open(
	        tmp / "doom.store.zip", ArchiveMount::Limits{});
	ASSERT_EQ(as_file.error, ArchiveMount::Error::None) << as_file.detail;
	EXPECT_EQ(leaf, as_file.reader->IdentityHash());
	EXPECT_EQ(drive->SidecarDirectory(),
	          tmp / "saves" / ("doom-" + leaf.substr(0, 8)));
	auto detail       = std::string();
	const auto record = ArchiveMount::SidecarRecord::Load(
	        drive->SidecarDirectory() / ArchiveMount::SidecarTomlName, &detail);
	ASSERT_TRUE(record) << detail;
	EXPECT_EQ(record->archive_path, ArchiveMount::PathToUtf8(house));
	EXPECT_EQ(record->identity_hash, leaf);
	auto file = drive->FileCreate("NEW.TXT", {});
	ASSERT_TRUE(file);
	file->Close();
	drive.reset();
	UnmountHelper('D');
	// Another collection around the same inner zip shares the saves (N5).
	const auto other = Built("other.zip",
	                         Kind::ZipStored,
	                         {
	                                 {     "info.toml","[game]\nname = \"other\"\n"                                            },
	                                 {"doom.store.zip",
	                                  ReadAll(tmp / "doom.store.zip")}
        });
	ASSERT_TRUE(Mount("E " + other.string()).has_value());
	ASSERT_TRUE(UnionAt('E'));
	EXPECT_TRUE(UnionAt('E')->FileExists("NEW.TXT"));
}

TEST_F(MountArchiveTest, AnEnteredDriveReconnects)
{
	const auto house = House("doom.zip");
	ASSERT_TRUE(Mount("D " + house.string()).has_value());
	auto drive = DriveAt('D');
	ASSERT_TRUE(drive);
	const auto before = drive->Source().IdentityHash();
	// The window reads the host: a touched host is Changed on the next
	// read, and only a drive out of Ready reconnects.
	std::error_code ec = {};
	std_fs::last_write_time(house,
	                        std_fs::last_write_time(house, ec) +
	                                std::chrono::minutes(1),
	                        ec);
	ASSERT_FALSE(ec) << ec.message();
	EXPECT_FALSE(drive->FileOpen("DOOM.EXE", OPEN_READ));
	ASSERT_EQ(drive->State(), ArchiveMountState::Changed);
	auto detail = std::string();
	EXPECT_TRUE(drive->Reconnect(&detail)) << detail;
	EXPECT_EQ(drive->State(), ArchiveMountState::Ready);
	EXPECT_EQ(drive->Source().IdentityHash(), before);
	EXPECT_TRUE(drive->FileExists("DOOM.EXE"));
	EXPECT_TRUE(drive->FileOpen("DOOM.EXE", OPEN_READ));
}

TEST_F(MountArchiveTest, AShortMemberPathMountsADiskFromAHouseArchive)
{
	const auto house = House("disks.zip", Images());
	ASSERT_TRUE(Mount("A " + house.string() + "/disks/disk1.img").has_value());
	ASSERT_TRUE(imageDiskList[0]);
	EXPECT_STREQ(Drives.at(0)->GetLabel(), "DISKONE");
	UnmountHelper('A');
	ASSERT_TRUE(Mount("A " + house.string() + "/disks.store.zip/disks/disk2.img")
	                    .has_value());
	ASSERT_TRUE(imageDiskList[0]);
	EXPECT_STREQ(Drives.at(0)->GetLabel(), "DISKTWO");
}

// G16: on a disc the house archive is a chain level, not the host file.
static std_fs::path DiscWith(const std_fs::path& tmp, const std_fs::path& archive)
{
	const auto iso = ArchiveFixtures::IsoWithFiles({
	        {"GAMES/HOUSE.ZIP", ReadAll(archive)}
        });
	ArchiveFixtures::WriteBytes(tmp / "disc.iso",
	                            std::vector<uint8_t>(iso.begin(), iso.end()));
	return tmp / "disc.iso";
}

TEST_F(MountArchiveTest, AShortMemberPathMountsADiskFromAHouseArchiveOnADisc)
{
	const auto disc = DiscWith(tmp, House("disks.zip", Images()));
	ASSERT_TRUE(Mount("A " + disc.string() + "/GAMES/HOUSE.ZIP/disks/disk1.img")
	                    .has_value());
	ASSERT_TRUE(imageDiskList[0]);
	EXPECT_STREQ(Drives.at(0)->GetLabel(), "DISKONE");
}

TEST_F(MountArchiveTest, AHouseArchiveOnADiscMountsAsItsGame)
{
	const auto disc = DiscWith(tmp, House("doom.zip"));
	ASSERT_TRUE(Mount("D " + disc.string() + "/GAMES/HOUSE.ZIP").has_value());
	ASSERT_TRUE(DriveAt('D'));
	EXPECT_TRUE(DriveAt('D')->FileExists("DOOM.EXE"));
}

TEST_F(MountArchiveTest, AShortChainThroughAHouseArchiveOnADiscMountsTheInnerArchive)
{
	const auto inner = Built("tdc.zip", Kind::ZipStored, Game());
	const auto disc  = DiscWith(tmp,
                                   House("doom.zip",
	                                  {
                                                 {"tdc.zip", ReadAll(inner)}
        }));
	ASSERT_TRUE(Mount("D " + disc.string() + "/GAMES/HOUSE.ZIP/tdc.zip").has_value());
	auto drive = DriveAt('D');
	ASSERT_TRUE(drive);
	EXPECT_TRUE(drive->FileExists("DOOM.EXE"));
	EXPECT_NE(drive->GetInfoString().find("GAMES/HOUSE.ZIP/doom.store.zip/tdc.zip"),
	          std::string::npos)
	        << drive->GetInfoString();
}

TEST_F(MountArchiveTest, AShortChainThroughAHouseArchiveMountsTheInnerArchive)
{
	// The host file itself is the house archive: its store holds tdc.zip.
	const auto inner = Built("tdc.zip", Kind::ZipStored, Game());
	const auto house = House("doom.zip",
	                         {
	                                 {"tdc.zip", ReadAll(inner)}
        });
	ASSERT_TRUE(Mount("D " + house.string() + "/tdc.zip").has_value());
	auto drive = DriveAt('D');
	ASSERT_TRUE(drive);
	EXPECT_TRUE(drive->FileExists("DOOM.EXE"));
	EXPECT_NE(drive->GetInfoString().find("doom.zip/doom.store.zip/tdc.zip"),
	          std::string::npos)
	        << drive->GetInfoString();
}

TEST_F(MountArchiveTest, ASecondArchiveIsRefusedNotDropped)
{
	// G12: MOUNT A d1.zip d2.zip mounted d1.zip and dropped d2.zip silently.
	const auto one = Built("d1.zip", Kind::ZipStored, Game());
	const auto two = Built("d2.zip", Kind::ZipStored, Game());
	EXPECT_FALSE(Mount("A " + one.string() + " " + two.string()).has_value());
	EXPECT_FALSE(Drives.at(drive_index('A')));
	ASSERT_FALSE(verdicts.empty());
	EXPECT_EQ(verdicts.back().second.reason, DenyReason::ArchiveOnePerDrive);
}

TEST_F(MountArchiveTest, AHouseArchiveWhoseMemberCannotBeEnteredIsRefused)
{
	// N3: the shape says enter; a member that is no archive refuses the
	// mount with its reason instead of falling back to a file listing.
	const auto junk = Built("junk.zip",
	                        Kind::ZipStored,
	                        {
	                                {"info.toml", "[game]\n"},
	                                {"junk.store.zip", std::string(5000, 'z')}
        });
	EXPECT_FALSE(Mount("D " + junk.string()).has_value());
	EXPECT_FALSE(Drives.at(drive_index('D')));
	ASSERT_FALSE(verdicts.empty());
	EXPECT_FALSE(verdicts.back().second.allowed);
	EXPECT_EQ(ArchiveMount::ArchiveHelperClient::LiveHelpers(), 0);
	// A sequential member follows archive_sequential (N8), as the leaf
	// would when spelled out.
	const auto seven = Built("inner.7z", Kind::SevenZip, Game());
	const auto house = Built("seven.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"info.toml",     "[game]\n"},
	                                 { "seven.7z", ReadAll(seven)}
        });
	EXPECT_FALSE(Mount("D " + house.string()).has_value());
	EXPECT_FALSE(Drives.at(drive_index('D')));
	Set("archive_sequential", "cache");
	ASSERT_TRUE(Mount("D " + house.string()).has_value());
	ASSERT_TRUE(DriveAt('D'));
	EXPECT_TRUE(DriveAt('D')->FileExists("DOOM.EXE"));
}

TEST_F(MountArchiveTest, AnIsoIsACdImageAndNotAnArchiveDrive)
{
	// N2: an ISO is a level in the middle of a chain or a CD at its end,
	// never a drive over its files; -t archive on one is refused.
	const auto iso = ArchiveFixtures::IsoWithFiles({
	        {"README.TXT", "x"}
        });
	ArchiveFixtures::WriteBytes(tmp / "disc.iso",
	                            std::vector<uint8_t>(iso.begin(), iso.end()));
	EXPECT_FALSE(
	        Mount("D " + (tmp / "disc.iso").string() + " -t archive").has_value());
	EXPECT_FALSE(Drives.at(drive_index('D')));
	ASSERT_FALSE(verdicts.empty());
	EXPECT_EQ(verdicts.back().second.reason, DenyReason::NotAnArchive);
	// Without the switch it is the CD image it always was.
	const auto plain = Mount("D " + (tmp / "disc.iso").string());
	ASSERT_TRUE(plain.has_value());
	EXPECT_EQ(plain->type, MountType::CdRomImage);
}

TEST_F(MountArchiveTest, ASpelledChainMountsTheInnerArchiveAsTheDrive)
{
	const auto outer = Outer("coll.zip", "doom.zip");
	auto result      = Mount("D " + outer.string() + "/doom.zip");
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->type, MountType::Archive);
	auto drive = DriveAt('D');
	ASSERT_TRUE(drive);
	EXPECT_TRUE(drive->FileExists("DOOM.EXE"));
	EXPECT_FALSE(drive->FileExists("NOTES.TXT"));
	EXPECT_NE(drive->GetInfoString().find("coll.zip/doom.zip"), std::string::npos)
	        << drive->GetInfoString();
	// The stored chain is a window: nothing copied.
	EXPECT_EQ(CacheCopies(tmp / "cache"), 0);
	drive.reset();
	UnmountHelper('D');
	EXPECT_EQ(ArchiveMount::ArchiveHelperClient::LiveHelpers(), 0);
}

TEST_F(MountArchiveTest, ASpelledChainThroughADeflatedOuterUsesTheCache)
{
	const auto outer = Outer("coll.zip", "doom.zip", Kind::ZipStored, Kind::ZipDeflate);
	ASSERT_TRUE(Mount("D " + outer.string() + "/doom.zip").has_value());
	ASSERT_TRUE(DriveAt('D'));
	EXPECT_TRUE(DriveAt('D')->FileExists("DOOM.EXE"));
	EXPECT_EQ(CacheCopies(tmp / "cache"), 1);
}

TEST_F(MountArchiveTest, ASpelledChainCountsItsLevelsAgainstTheDepth)
{
	// N1: three levels below the host mount; a fourth is refused with
	// the reason Resolve gives a fourth image level.
	const auto c     = Built("c.zip", Kind::ZipStored, Game());
	const auto b     = Built("b.zip",
                             Kind::ZipStored,
	                         {
                                     {"c.zip", ReadAll(c)}
        });
	const auto a     = Built("a.zip",
                             Kind::ZipStored,
	                         {
                                     {"b.zip", ReadAll(b)}
        });
	const auto outer = Built("outer.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"a.zip", ReadAll(a)}
        });
	ASSERT_TRUE(Mount("D " + outer.string() + "/a.zip/b.zip/c.zip").has_value());
	ASSERT_TRUE(DriveAt('D'));
	EXPECT_TRUE(DriveAt('D')->FileExists("DOOM.EXE"));
	UnmountHelper('D');
	const auto four = Built("four.zip",
	                        Kind::ZipStored,
	                        {
	                                {"outer.zip", ReadAll(outer)}
        });
	EXPECT_FALSE(
	        Mount("E " + four.string() + "/outer.zip/a.zip/b.zip/c.zip").has_value());
	EXPECT_FALSE(Drives.at(drive_index('E')));
	ASSERT_FALSE(verdicts.empty());
	EXPECT_EQ(verdicts.back().second.reason, DenyReason::ArchiveEntryRejected);
}

TEST_F(MountArchiveTest, ASpelledChainWhoseLeafIsAHouseArchiveEntersIt)
{
	// N3 applies to the leaf of a chain as to a plain host: a collection
	// of house zips.
	const auto house = House("doom.zip");
	const auto coll  = Built("coll.zip",
                                Kind::ZipStored,
	                         {
                                        {"readme.txt",            "x"},
                                        {  "doom.zip", ReadAll(house)}
        });
	ASSERT_TRUE(Mount("D " + coll.string() + "/doom.zip").has_value());
	auto drive = DriveAt('D');
	ASSERT_TRUE(drive);
	EXPECT_TRUE(drive->FileExists("DOOM.EXE"));
	EXPECT_FALSE(drive->FileExists("INFO.TOML"));
	EXPECT_NE(drive->GetInfoString().find("doom.store.zip"), std::string::npos)
	        << drive->GetInfoString();
	drive.reset();
	UnmountHelper('D');
	// At the depth cap the house step cannot enter: refused, not listed.
	const auto b     = Built("b.zip",
                             Kind::ZipStored,
	                         {
                                     {"doom.zip", ReadAll(house)}
        });
	const auto a     = Built("a.zip",
                             Kind::ZipStored,
	                         {
                                     {"b.zip", ReadAll(b)}
        });
	const auto outer = Built("outer.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"a.zip", ReadAll(a)}
        });
	EXPECT_FALSE(
	        Mount("E " + outer.string() + "/a.zip/b.zip/doom.zip").has_value());
	EXPECT_FALSE(Drives.at(drive_index('E')));
	EXPECT_EQ(verdicts.back().second.reason, DenyReason::ArchiveEntryRejected);
}

TEST_F(MountArchiveTest, ASpelledChainsSidecarIsKeyedOnTheLeafUnderTheHostsName)
{
	const auto outer = Outer("coll.zip", "doom.zip");
	ASSERT_TRUE(Mount("D " + outer.string() + "/doom.zip").has_value());
	auto drive = UnionAt('D');
	ASSERT_TRUE(drive);
	const auto leaf    = ArchiveDriveOf(drive)->Source().IdentityHash();
	const auto as_file = ArchiveMount::ArchiveReader::Open(tmp / "doom.zip",
	                                                       ArchiveMount::Limits{});
	ASSERT_EQ(as_file.error, ArchiveMount::Error::None) << as_file.detail;
	EXPECT_EQ(leaf, as_file.reader->IdentityHash());
	EXPECT_EQ(drive->SidecarDirectory(),
	          tmp / "saves" / ("coll-" + leaf.substr(0, 8)));
	auto detail       = std::string();
	const auto record = ArchiveMount::SidecarRecord::Load(
	        drive->SidecarDirectory() / ArchiveMount::SidecarTomlName, &detail);
	ASSERT_TRUE(record) << detail;
	// The record names the chain, so --sidecar list can follow it.
	EXPECT_EQ(record->archive_path, ArchiveMount::PathToUtf8(outer) + "/doom.zip");
	EXPECT_EQ(record->identity_hash, leaf);
	auto file = drive->FileCreate("NEW.TXT", {});
	ASSERT_TRUE(file);
	file->Close();
	drive.reset();
	UnmountHelper('D');
	// The same inner zip under another collection shares the saves (N5).
	const auto other = Built("other.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"notes.txt",                       "y"},
	                                 { "doom.zip", ReadAll(tmp / "doom.zip")}
        });
	ASSERT_TRUE(Mount("E " + other.string() + "/doom.zip").has_value());
	ASSERT_TRUE(UnionAt('E'));
	EXPECT_TRUE(UnionAt('E')->FileExists("NEW.TXT"));
}

TEST_F(MountArchiveTest, ASpelledChainDriveReconnects)
{
	// As AnEnteredDriveReconnects: the window reads the host, a touched
	// host is Changed, and only a drive out of Ready reconnects.
	const auto outer = Outer("coll.zip", "doom.zip");
	ASSERT_TRUE(Mount("D " + outer.string() + "/doom.zip").has_value());
	auto drive = DriveAt('D');
	ASSERT_TRUE(drive);
	const auto before  = drive->Source().IdentityHash();
	std::error_code ec = {};
	std_fs::last_write_time(outer,
	                        std_fs::last_write_time(outer, ec) +
	                                std::chrono::minutes(1),
	                        ec);
	ASSERT_FALSE(ec) << ec.message();
	EXPECT_FALSE(drive->FileOpen("DOOM.EXE", OPEN_READ));
	ASSERT_EQ(drive->State(), ArchiveMountState::Changed);
	auto detail = std::string();
	EXPECT_TRUE(drive->Reconnect(&detail)) << detail;
	EXPECT_EQ(drive->State(), ArchiveMountState::Ready);
	EXPECT_EQ(drive->Source().IdentityHash(), before);
	EXPECT_TRUE(drive->FileOpen("DOOM.EXE", OPEN_READ));
}

TEST_F(MountArchiveTest, AMissingLevelInASpelledChainIsRefusedAndHoldsNothing)
{
	const auto outer = Outer("coll.zip", "doom.zip");
	EXPECT_FALSE(Mount("D " + outer.string() + "/none.zip").has_value());
	EXPECT_FALSE(Drives.at(drive_index('D')));
	ASSERT_FALSE(verdicts.empty());
	EXPECT_EQ(verdicts.back().second.reason, DenyReason::ArchiveMemberNotFound);
	EXPECT_EQ(ArchiveMount::ArchiveHelperClient::LiveHelpers(), 0);
	// An image leaf keeps the image route: the dispatch looks at the
	// leaf's name, not at the chain.
	const auto images = Outer("imgs.zip",
	                          "game.zip",
	                          Kind::ZipStored,
	                          Kind::ZipStored,
	                          Images());
	const auto result = Mount("A " + images.string() + "/game.zip/disks/disk1.img");
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->type, MountType::FloppyImage);
}

TEST_F(MountArchiveTest, AReconnectOfAnEnteredDriveReleasesTheOldHostsEntry)
{
	// A deflated member is entered through the cache: the drive holds a
	// lease on the host's entry. After the host changes and the drive
	// re-enters, the fresh entry is held and the old one is let go.
	const auto inner   = Built("doom.store.zip", Kind::ZipStored, Game());
	const auto house   = Built("doom.zip",
                                 Kind::ZipDeflate,
	                           {
                                         {     "info.toml", "[game]\nname = \"v1\"\n"},
                                         {"doom.store.zip",            ReadAll(inner)}
        });
	const auto hash_of = [&](const std_fs::path& p) {
		auto opened = ArchiveMount::ArchiveReader::Open(p, ArchiveMount::Limits{});
		EXPECT_EQ(opened.error, ArchiveMount::Error::None) << opened.detail;
		return opened.reader ? opened.reader->IdentityHash()
		                     : std::string();
	};
	const auto old_entry = ArchiveMount::ArchiveCache::EntryDirectory(
	        tmp / "cache", "doom", hash_of(house));
	ASSERT_TRUE(Mount("D " + house.string()).has_value());
	auto drive = DriveAt('D');
	ASSERT_TRUE(drive);
	EXPECT_TRUE(ArchiveMount::FileLock::IsHeld(old_entry / ArchiveMount::LockName));
	// The leaf reads the cache copy: a touched copy is Changed on the
	// next read, which takes the drive out of Ready.
	std::error_code ec = {};
	auto copies        = 0;
	for (const auto& item :
	     std_fs::directory_iterator(old_entry / ArchiveMount::FilesDirName, ec)) {
		std_fs::last_write_time(item.path(),
		                        std_fs::last_write_time(item.path(), ec) +
		                                std::chrono::minutes(1),
		                        ec);
		++copies;
	}
	ASSERT_EQ(copies, 1);
	EXPECT_FALSE(drive->FileOpen("DOOM.EXE", OPEN_READ));
	ASSERT_EQ(drive->State(), ArchiveMountState::Changed);
	ASSERT_TRUE(WriteArchive(house,
	                         Kind::ZipDeflate,
	                         {
	                                 {     "info.toml", "[game]\nname = \"v2\"\n"},
	                                 {"doom.store.zip",            ReadAll(inner)}
        }));
	const auto new_entry = ArchiveMount::ArchiveCache::EntryDirectory(
	        tmp / "cache", "doom", hash_of(house));
	ASSERT_NE(old_entry, new_entry);
	auto detail = std::string();
	ASSERT_TRUE(drive->Reconnect(&detail)) << detail;
	EXPECT_TRUE(drive->FileExists("DOOM.EXE"));
	EXPECT_FALSE(ArchiveMount::FileLock::IsHeld(old_entry /
	                                            ArchiveMount::LockName));
	EXPECT_TRUE(ArchiveMount::FileLock::IsHeld(new_entry / ArchiveMount::LockName));
}

TEST_F(MountArchiveTest, JunkWithAnArchiveExtensionIsRefused)
{
	const auto junk = tmp / "notes.zip";
	{
		std::ofstream out(junk, std::ios::binary);
		out << std::string(600, 'a');
	}
	// Without -t: not an archive by magic, not an image either.
	EXPECT_FALSE(Mount("D " + junk.string()).has_value());
	// With -t archive: the policy says NotAnArchive.
	EXPECT_FALSE(Mount("D " + junk.string() + " -t archive").has_value());
	ASSERT_FALSE(verdicts.empty());
	EXPECT_EQ(verdicts.back().second.reason, DenyReason::NotAnArchive);
	EXPECT_TRUE(constructed.empty());
	// -t archive on a directory.
	EXPECT_FALSE(Mount("D " + tmp.string() + " -t archive").has_value());
}

TEST_F(MountArchiveTest, SequentialIsRefusedByDefaultAndCachedOnRequest)
{
	const auto tgz = Built("doom.tar.gz", Kind::TarGz);
	EXPECT_FALSE(Mount("D " + tgz.string()).has_value());
	ASSERT_FALSE(verdicts.empty());
	EXPECT_EQ(verdicts.back().second.reason, DenyReason::ArchiveSequential);
	EXPECT_EQ(ArchiveMount::ArchiveHelperClient::LiveHelpers(), 0);
	Set("archive_sequential", "cache");
	const auto result = Mount("D " + tgz.string());
	ASSERT_TRUE(result.has_value());
	auto drive = DriveAt('D');
	ASSERT_TRUE(drive);
	auto file = drive->FileOpen("README.TXT", OPEN_READ);
	ASSERT_TRUE(file);
	// The one pass landed in the cache entry.
	const auto entry = ArchiveMount::ArchiveCache::EntryDirectory(
	        tmp / "cache", "doom.tar", drive->Source().IdentityHash());
	EXPECT_TRUE(std_fs::is_regular_file(entry / ArchiveMount::FilesDirName / "1"));
}

// Design 6.1: MOUNT -cache shows the store, MOUNT -cache trim runs D14's
// cleanup now.
TEST_F(MountArchiveTest, CacheStatusTextNamesSizeCapTtlAndFreeSpace)
{
	ArchiveManagement::CacheOverview o  = {};
	o.exists                            = true;
	o.directory                         = "/c";
	o.bytes                             = 3 << 20;
	o.cap_bytes                         = 4096ll << 20;
	o.min_free_bytes                    = 2048ll << 20;
	o.ttl_seconds                       = 7 * 86400;
	o.free_bytes                        = 10ll << 30;
	ArchiveManagement::CacheEntryInfo e = {};
	e.directory                         = "/c/g/game-1a2b3c4d";
	e.archive_path                      = "/games/game.zip";
	e.bytes                             = 2 << 20;
	e.last_use                          = 0;
	o.entries.push_back(e);
	const auto text = MOUNT::CacheOverviewText(o);
	EXPECT_NE(text.find("3 MiB"), std::string::npos) << text;
	EXPECT_NE(text.find("4096 MiB"), std::string::npos) << text;
	EXPECT_NE(text.find("7 days"), std::string::npos) << text;
	EXPECT_NE(text.find("10240 MiB free"), std::string::npos) << text;
	EXPECT_NE(text.find("game-1a2b3c4d"), std::string::npos) << text;
	EXPECT_NE(text.find("/games/game.zip"), std::string::npos) << text;

	ArchiveManagement::CacheOverview none = {};
	none.directory                        = "/c";
	EXPECT_NE(MOUNT::CacheOverviewText(none).find("no cache yet"),
	          std::string::npos);
}

// ada-pbr6: the summary and the entry lines wrapped past 80 columns. Paths
// stand on lines of their own; everything else fits.
TEST_F(MountArchiveTest, CacheStatusTextFitsEightyColumns)
{
	const std::string cache_dir = "/home/player/.cache/dosbox-automation/" +
	                              std::string(50, 'c');
	const std::string archive = "/home/player/Games/collections/" +
	                            std::string(60, 'a') + ".tar.xz";
	ArchiveManagement::CacheOverview o  = {};
	o.exists                            = true;
	o.directory                         = cache_dir;
	o.bytes                             = 123456ll << 20;
	o.cap_bytes                         = 409600ll << 20;
	o.min_free_bytes                    = 204800ll << 20;
	o.ttl_seconds                       = 365 * 86400;
	o.free_bytes                        = 9999999ll << 20;
	ArchiveManagement::CacheEntryInfo e = {};
	e.directory    = cache_dir + "/k/kyra2.tar-9c79bfcc";
	e.archive_path = archive;
	e.bytes        = 65536ll << 20;
	e.last_use     = 0;
	e.locked       = true;
	o.entries.push_back(e);
	const auto text = MOUNT::CacheOverviewText(o);
	auto path_lines = 0;
	for (const auto part : std::views::split(std::string_view(text), '\n')) {
		const auto line = std::string(part.begin(), part.end());
		if (line == "  " + cache_dir || line == "    " + archive) {
			++path_lines;
			continue;
		}
		EXPECT_LE(line.size(), 79u) << line;
	}
	EXPECT_EQ(path_lines, 2) << text;
	EXPECT_NE(text.find("(in use)"), std::string::npos) << text;
}

// An entry without a readable record has no archive and no last use to
// show; it says so instead of counting days from the epoch.
TEST_F(MountArchiveTest, CacheStatusTextMarksADamagedEntry)
{
	ArchiveManagement::CacheOverview o  = {};
	o.exists                            = true;
	o.directory                         = "/c";
	o.cap_bytes                         = 4096ll << 20;
	o.ttl_seconds                       = 7 * 86400;
	ArchiveManagement::CacheEntryInfo e = {};
	e.directory                         = "/c/g/game-1a2b3c4d";
	e.bytes                             = 2 << 20;
	e.damaged                           = true;
	o.entries.push_back(e);
	const auto text = MOUNT::CacheOverviewText(o);
	EXPECT_NE(text.find("game-1a2b3c4d  2 MiB  damaged"), std::string::npos)
	        << text;
	EXPECT_EQ(text.find("days ago"), std::string::npos) << text;
}

// MOUNT -cache runs the same path check as --archive-cache status: a link
// or a system path is named, never listed as a cache.
TEST_F(MountArchiveTest, CacheStatusForALinkedRootNamesTheLink)
{
	std_fs::create_directories(tmp / "real");
	std_fs::create_directory_symlink(tmp / "real", tmp / "link");
	Set("archive_cache_path", (tmp / "link").string());
	const auto text = MOUNT::CacheOverviewForSettings();
	EXPECT_NE(text.find("is a link"), std::string::npos) << text;
	EXPECT_EQ(text.find("not a directory"), std::string::npos) << text;
	EXPECT_EQ(text.find("no entries"), std::string::npos) << text;
}

TEST_F(MountArchiveTest, CacheTrimRemovesAnExpiredEntry)
{
	const auto entry = ExpiredCacheEntry();
	SetCommand("-cache trim");
	EXPECT_TRUE(program->HandleCache());
	EXPECT_FALSE(std_fs::exists(entry));
}

// The first open of the store runs its own cleanup; -cache trim reports
// what that removed, not a second pass that finds nothing.
TEST_F(MountArchiveTest, CacheTrimReportsWhatTheFirstOpenRemoved)
{
	const auto entry = ExpiredCacheEntry();
	MOUNT::ResetArchiveCacheForTests();
	auto detail       = std::string();
	const auto report = MOUNT::TrimCache(&detail);
	ASSERT_TRUE(report) << detail;
	EXPECT_EQ(report->removed_expired, 1);
	EXPECT_FALSE(std_fs::exists(entry));
}

TEST_F(MountArchiveTest, CacheTrimTakesTheWordInAnyCase)
{
	const auto entry = ExpiredCacheEntry();
	SetCommand("-cache TRIM");
	EXPECT_TRUE(program->HandleCache());
	EXPECT_FALSE(std_fs::exists(entry));
}

TEST_F(MountArchiveTest, CacheIsHandledWithOrWithoutAWordAndNotForOtherLines)
{
	const auto entry = ExpiredCacheEntry();
	SetCommand("-cache");
	EXPECT_TRUE(program->HandleCache());
	EXPECT_TRUE(std_fs::exists(entry)); // the overview removes nothing
	SetCommand("-cache wipe");
	EXPECT_TRUE(program->HandleCache());
	EXPECT_TRUE(std_fs::exists(entry)); // an unknown word trims nothing
	SetCommand("D game.zip");
	EXPECT_FALSE(program->HandleCache());
}

TEST_F(MountArchiveTest, NoSidecarParsesAndAnUnavailableCacheRefuses)
{
	const auto zip = Built("doom.zip", Kind::ZipStored);
	auto result    = Mount("D " + zip.string() + " -nosidecar");
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(result->no_sidecar);
	UnmountHelper('D');
	{
		std::ofstream out(tmp / "file", std::ios::binary);
		out << "x";
	}
	Set("archive_cache_path", (tmp / "file").string());
	MOUNT::ResetArchiveCacheForTests();
	EXPECT_FALSE(Mount("D " + zip.string()).has_value());
	EXPECT_EQ(ArchiveMount::ArchiveHelperClient::LiveHelpers(), 0);
	EXPECT_FALSE(Drives.at(drive_index('D')));
}

TEST_F(MountArchiveTest, TheCapsFromTheSettingsReachTheHelper)
{
	const auto zip = Built("doom.zip", Kind::ZipStored);
	Set("archive_max_entries", "2");
	EXPECT_FALSE(Mount("D " + zip.string()).has_value());
	ASSERT_FALSE(verdicts.empty());
	// The policy's own verdict was allowed; the refusal came from the
	// index, reported through the same hook with the mapped reason.
	EXPECT_EQ(verdicts.back().second.reason, DenyReason::ArchiveTooLarge);
	EXPECT_EQ(ArchiveMount::ArchiveHelperClient::LiveHelpers(), 0);
	// The scratch directory made for the helper went with the refusal.
	auto scratch_entries = 0;
	std::error_code ec   = {};
	for (const auto& e :
	     std_fs::directory_iterator(tmp / "cache" / "scratch", ec)) {
		(void)e;
		++scratch_entries;
	}
	EXPECT_EQ(scratch_entries, 0);
}

TEST_F(MountArchiveTest, ReconnectCommandOnlyTouchesArchiveDrives)
{
	const auto zip = Built("doom.zip", Kind::ZipStored);
	ASSERT_TRUE(Mount("D " + zip.string()).has_value());
	SetCommand("-reconnect D:");
	EXPECT_TRUE(program->HandleReconnect());
	EXPECT_EQ(DriveAt('D')->State(), ArchiveMountState::Ready);
	SetCommand("-reconnect Z:");
	// Handled: the message says it is no archive.
	EXPECT_TRUE(program->HandleReconnect());
	SetCommand("D " + zip.string());
	EXPECT_FALSE(program->HandleReconnect());
}

TEST_F(MountArchiveTest, TheOverlapCheckRefusesAMountedLetter)
{
	const auto zip = Built("doom.zip", Kind::ZipStored);
	ASSERT_TRUE(Mount("D " + zip.string()).has_value());
	EXPECT_FALSE(Mount("D " + zip.string()).has_value());
	EXPECT_EQ(ArchiveMount::ArchiveHelperClient::LiveHelpers(), 1);
}

TEST_F(MountArchiveTest, AMountGetsASidecarUnderSavesAndWritesLandThere)
{
	const auto zip = Built("doom.zip", Kind::ZipStored);
	auto result    = Mount("D " + zip.string());
	ASSERT_TRUE(result.has_value());
	auto drive = UnionAt('D');
	ASSERT_TRUE(drive);
	const auto hash = ArchiveDriveOf(drive)->Source().IdentityHash();
	EXPECT_EQ(drive->SidecarDirectory(),
	          tmp / "saves" / ("doom-" + hash.substr(0, 8)));
	EXPECT_TRUE(std_fs::is_regular_file(drive->SidecarDirectory() /
	                                    ArchiveMount::SidecarTomlName));
	EXPECT_TRUE(std_fs::is_regular_file(drive->SidecarDirectory() / ".lock"));
	auto file = drive->FileCreate("NEW.TXT", {});
	ASSERT_TRUE(file);
	file->Close();
	EXPECT_TRUE(std_fs::is_regular_file(drive->SidecarDirectory() / "NEW.TXT"));
	EXPECT_EQ(drive->GetType(), DosDriveType::Archive);
	drive.reset();
	UnmountHelper('D');
	// A remount finds the same sidecar by path.
	result = Mount("E " + zip.string());
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(UnionAt('E')->FileExists("NEW.TXT"));
}

// E6 (gate 2026-10-17): a saves directory the path check refuses stops the
// mount; an empty directory would put the sidecar in the working directory.
class RefusedSavesTest : public MountArchiveTest {
protected:
	void SetUp() override
	{
		MountArchiveTest::SetUp();
		for (const auto& entry : MountPolicy::SystemPaths()) {
			if (entry.has_relative_path()) {
				Set("archive_sidecar_path",
				    (entry / "da-saves-test").string());
				break;
			}
		}
		previous_cwd = std_fs::current_path();
		std_fs::create_directories(tmp / "cwd");
		std_fs::current_path(tmp / "cwd");
	}
	void TearDown() override
	{
		std_fs::current_path(previous_cwd);
		MountArchiveTest::TearDown();
	}
	bool CwdIsEmpty() const
	{
		return std_fs::is_empty(tmp / "cwd");
	}
	std_fs::path previous_cwd = {};
};

TEST_F(RefusedSavesTest, TheDriveMountIsRefusedAndWritesNothing)
{
	const auto zip = Built("doom.zip", Kind::ZipStored);
	EXPECT_FALSE(Mount("D " + zip.string()).has_value());
	EXPECT_EQ(Drives.at(drive_index('D')), nullptr);
	EXPECT_TRUE(CwdIsEmpty());
}

TEST_F(RefusedSavesTest, TheImageMountIsRefusedAndWritesNothing)
{
	const auto zip = Built("images.zip", Kind::ZipDeflate, Images());
	EXPECT_FALSE(Mount("A " + (zip / "disks" / "disk1.img").string()).has_value());
	EXPECT_EQ(Drives.at(0), nullptr);
	EXPECT_TRUE(CwdIsEmpty());
}

// The design follows a saves directory linked elsewhere (another disk), as
// the --sidecar lister does; the system-path rule judges where it points.
TEST_F(MountArchiveTest, ALinkedSavesDirectoryIsFollowed)
{
	std_fs::create_directories(tmp / "elsewhere");
	std_fs::create_directory_symlink(tmp / "elsewhere", tmp / "linked");
	Set("archive_sidecar_path", (tmp / "linked").string());
	const auto zip = Built("doom.zip", Kind::ZipStored);
	ASSERT_TRUE(Mount("D " + zip.string()).has_value());
	auto drive = UnionAt('D');
	ASSERT_TRUE(drive);
	EXPECT_TRUE(std_fs::is_regular_file(tmp / "elsewhere" /
	                                    drive->SidecarDirectory().filename() /
	                                    ArchiveMount::SidecarTomlName));
}

TEST_F(MountArchiveTest, ASavesLinkIntoASystemPathIsRefused)
{
	auto system_dir = std_fs::path();
	for (const auto& entry : MountPolicy::SystemPaths()) {
		if (entry.has_relative_path() && std_fs::is_directory(entry)) {
			system_dir = entry;
			break;
		}
	}
	ASSERT_FALSE(system_dir.empty());
	std_fs::create_directory_symlink(system_dir, tmp / "linked");
	Set("archive_sidecar_path", (tmp / "linked").string());
	const auto zip = Built("doom.zip", Kind::ZipStored);
	EXPECT_FALSE(Mount("D " + zip.string()).has_value());
}

// A place the check cannot resolve is not a place it has cleared.
TEST_F(MountArchiveTest, ASavesLinkThatCannotBeResolvedIsRefused)
{
	std_fs::create_directory_symlink(tmp / "b", tmp / "a");
	std_fs::create_directory_symlink(tmp / "a", tmp / "b");
	const auto problem = MountArchiveInternal::SystemPathProblem(tmp / "a" /
	                                                             "saves");
	EXPECT_NE(problem.find("cannot be resolved"), std::string::npos) << problem;
}

TEST_F(MountArchiveTest, ARefusedSavesLinkNamesWhereItLeads)
{
	auto system_dir = std_fs::path();
	for (const auto& entry : MountPolicy::SystemPaths()) {
		if (entry.has_relative_path() && std_fs::is_directory(entry)) {
			system_dir = entry;
			break;
		}
	}
	ASSERT_FALSE(system_dir.empty());
	std_fs::create_directory_symlink(system_dir, tmp / "linked");
	const auto problem = MountArchiveInternal::SystemPathProblem(tmp / "linked");
	EXPECT_NE(problem.find(std_fs::canonical(system_dir).string()),
	          std::string::npos)
	        << problem;
}

#if !defined(WIN32)
TEST_F(MountArchiveTest, AnExistingSavesDirectoryKeepsItsMode)
{
	const auto saves = tmp / "saves";
	std_fs::create_directories(saves);
	std_fs::permissions(saves,
	                    std_fs::perms::owner_all | std_fs::perms::group_read |
	                            std_fs::perms::group_exec |
	                            std_fs::perms::others_read |
	                            std_fs::perms::others_exec);
	const auto before = std_fs::status(saves).permissions();
	const auto zip    = Built("doom.zip", Kind::ZipStored);
	ASSERT_TRUE(Mount("D " + zip.string()).has_value());
	EXPECT_EQ(std_fs::status(saves).permissions(), before);
}
#endif

TEST_F(MountArchiveTest, NoSidecarMountsTheArchiveDriveAlone)
{
	const auto zip = Built("doom.zip", Kind::ZipStored);
	ASSERT_TRUE(Mount("D " + zip.string() + " -nosidecar").has_value());
	EXPECT_TRUE(DriveAt('D'));
	EXPECT_FALSE(UnionAt('D'));
	EXPECT_FALSE(std_fs::exists(tmp / "saves"));
}

// Phase 6.4: -scratch is a mode of its own; the three switches that
// decide where writes go cannot be combined with it.
TEST_F(MountArchiveTest, ScratchParsesAndRefusesItsConflicts)
{
	const auto zip   = Built("doom.zip", Kind::ZipStored);
	const auto plain = Mount("D " + zip.string() + " -scratch");
	ASSERT_TRUE(plain.has_value());
	EXPECT_TRUE(plain->scratch);
	UnmountHelper('D');
	const auto named = " -sidecar " + (tmp / "named").string();
	for (const auto& conflict :
	     {std::string(" -ro"), std::string(" -nosidecar"), named}) {
		const auto line = "E " + zip.string() + " -scratch" + conflict;
		EXPECT_FALSE(Mount(line).has_value()) << line;
		EXPECT_FALSE(Drives.at(drive_index('E'))) << line;
	}
}

TEST_F(MountArchiveTest, ScratchOnADirectoryMountIsRefused)
{
	std_fs::create_directories(tmp / "plain");
	EXPECT_FALSE(Mount("E " + (tmp / "plain").string() + " -scratch").has_value());
	EXPECT_FALSE(Drives.at(drive_index('E')));
}

TEST_F(MountArchiveTest, ASecondMountOfTheSameArchiveIsSidecarBusy)
{
	const auto zip = Built("doom.zip", Kind::ZipStored);
	ASSERT_TRUE(Mount("D " + zip.string()).has_value());
	EXPECT_FALSE(Mount("E " + zip.string()).has_value());
	ASSERT_FALSE(verdicts.empty());
	EXPECT_EQ(verdicts.back().second.reason, DenyReason::SidecarBusy);
	EXPECT_FALSE(Drives.at(drive_index('E')));
	// With -nosidecar beside it: allowed, it writes nothing.
	EXPECT_TRUE(Mount("E " + zip.string() + " -nosidecar").has_value());
	// Two roots of one archive are still one sidecar: refused too.
	EXPECT_FALSE(Mount("F " + zip.string() + " -root game").has_value());
}

TEST_F(MountArchiveTest, AnExplicitSidecarPathIsUsedAndValidated)
{
	const auto zip  = Built("doom.zip", Kind::ZipStored);
	const auto mine = tmp / "mine";
	std_fs::create_directories(mine);
	ASSERT_TRUE(Mount("D " + zip.string() + " -sidecar " + mine.string()).has_value());
	EXPECT_EQ(UnionAt('D')->SidecarDirectory(), std_fs::canonical(mine));
	UnmountHelper('D');
	EXPECT_FALSE(Mount("D " + zip.string() + " -sidecar " + (tmp / "nope").string())
	                     .has_value());
	EXPECT_EQ(verdicts.back().second.reason, DenyReason::DoesNotResolve);
}

// The fixture has no PSP file table: handles go through FCB mode.
constexpr bool fcb = true;

// The union's upper side under a -scratch mount; every archive mount also
// owns a helper scratch beside it, so the tests name this one directly.
static bool IsScratchOf(const std_fs::path& dir, const std_fs::path& cache)
{
	std::error_code ec = {};
	return std_fs::equivalent(dir.parent_path(), cache / "scratch", ec) && !ec;
}

// Phase 6.4: the union's upper side is a scratch under the cache; the
// writes live there and go with the unmount, no sidecar appears.
TEST_F(MountArchiveTest, ScratchMountTakesWritesAndLosesThemAtUnmount)
{
	const auto zip = Built("doom.zip", Kind::ZipStored);
	ASSERT_TRUE(Mount("D " + zip.string() + " -scratch").has_value());
	ASSERT_TRUE(UnionAt('D'));
	const auto upper = UnionAt('D')->SidecarDirectory();
	EXPECT_TRUE(IsScratchOf(upper, tmp / "cache")) << upper;
	uint16_t handle = 0;
	ASSERT_TRUE(DOS_CreateFile("D:\\SAVE.DAT", {}, &handle, fcb));
	ASSERT_TRUE(DOS_CloseFile(handle, fcb));
	ASSERT_TRUE(DOS_MakeDir("D:\\SAVES"));
	EXPECT_TRUE(std_fs::is_regular_file(upper / "SAVE.DAT"));
	EXPECT_TRUE(std_fs::is_directory(upper / "SAVES"));
	EXPECT_FALSE(std_fs::exists(tmp / "saves"));
	UnmountHelper('D');
	EXPECT_FALSE(std_fs::exists(upper));
	EXPECT_FALSE(std_fs::exists(tmp / "saves"));
	EXPECT_FALSE(Drives.at(drive_index('D')));
}

TEST_F(MountArchiveTest, ScratchMountReadsTheArchive)
{
	const auto zip = Built("doom.zip", Kind::ZipStored);
	ASSERT_TRUE(Mount("D " + zip.string() + " -scratch").has_value());
	uint16_t handle = 0;
	ASSERT_TRUE(DOS_OpenFile("D:\\README.TXT", OPEN_READ, &handle, fcb));
	ASSERT_TRUE(DOS_CloseFile(handle, fcb));
	UnmountHelper('D');
}

// Two scratch mounts of one archive never share a directory, and each
// unmount takes only its own.
TEST_F(MountArchiveTest, TwoScratchMountsGetTwoDirectories)
{
	const auto zip = Built("doom.zip", Kind::ZipStored);
	ASSERT_TRUE(Mount("D " + zip.string() + " -scratch").has_value());
	ASSERT_TRUE(Mount("E " + zip.string() + " -scratch").has_value());
	ASSERT_TRUE(UnionAt('D'));
	ASSERT_TRUE(UnionAt('E'));
	const auto upper_d = UnionAt('D')->SidecarDirectory();
	const auto upper_e = UnionAt('E')->SidecarDirectory();
	EXPECT_NE(upper_d, upper_e);
	EXPECT_TRUE(IsScratchOf(upper_d, tmp / "cache")) << upper_d;
	EXPECT_TRUE(IsScratchOf(upper_e, tmp / "cache")) << upper_e;
	UnmountHelper('D');
	EXPECT_FALSE(std_fs::exists(upper_d));
	EXPECT_TRUE(std_fs::exists(upper_e));
	UnmountHelper('E');
	EXPECT_FALSE(std_fs::exists(upper_e));
}

// A crash leaves the scratch with a free lock; the next start's cleanup
// takes it, subdirectories and all.
TEST_F(MountArchiveTest, AScratchLeftByACrashIsSweptAtTheNextStart)
{
	const auto zip = Built("doom.zip", Kind::ZipStored);
	ASSERT_TRUE(Mount("D " + zip.string() + " -scratch").has_value());
	ASSERT_TRUE(UnionAt('D'));
	const auto upper = UnionAt('D')->SidecarDirectory();
	uint16_t handle  = 0;
	ASSERT_TRUE(DOS_CreateFile("D:\\SAVE.DAT", {}, &handle, fcb));
	ASSERT_TRUE(DOS_CloseFile(handle, fcb));
	ASSERT_TRUE(DOS_MakeDir("D:\\SAVES"));
	// The process and its locks go; nothing discards the directory.
	MOUNT::ResetArchiveCacheForTests();
	UnmountHelper('D');
	ASSERT_TRUE(std_fs::is_directory(upper / "SAVES"));
	auto detail = std::string();
	ASSERT_TRUE(MOUNT::TrimCache(&detail)) << detail;
	EXPECT_FALSE(std_fs::exists(upper));
}

TEST_F(MountArchiveTest, AnArchiveFileOpenOnTheUnionKeepsTheDriveMounted)
{
	const auto zip = Built("doom.zip", Kind::ZipStored);
	ASSERT_TRUE(Mount("D " + zip.string()).has_value());
	uint16_t handle = 0;
	ASSERT_TRUE(DOS_OpenFile("D:\\README.TXT", OPEN_READ, &handle, fcb));
	UnmountHelper('D');
	EXPECT_TRUE(UnionAt('D'));
	// The sidecar is still this mount's: a second mount of it is refused.
	EXPECT_FALSE(Mount("E " + zip.string()).has_value());
	ASSERT_TRUE(DOS_CloseFile(handle, fcb));
	UnmountHelper('D');
	EXPECT_FALSE(Drives.at(drive_index('D')));
	EXPECT_TRUE(Mount("E " + zip.string()).has_value());
}

TEST_F(MountArchiveTest, ASidecarFileOpenOnTheUnionKeepsTheDriveMounted)
{
	const auto zip = Built("doom.zip", Kind::ZipStored);
	ASSERT_TRUE(Mount("D " + zip.string()).has_value());
	uint16_t handle = 0;
	ASSERT_TRUE(DOS_CreateFile("D:\\SAVE.DAT", {}, &handle, fcb));
	UnmountHelper('D');
	EXPECT_TRUE(UnionAt('D'));
	ASSERT_TRUE(DOS_CloseFile(handle, fcb));
	UnmountHelper('D');
	EXPECT_FALSE(Drives.at(drive_index('D')));
}

TEST_F(MountArchiveTest, AFileOpenOnAPlainArchiveDriveKeepsItMounted)
{
	const auto zip = Built("doom.zip", Kind::ZipStored);
	ASSERT_TRUE(Mount("D " + zip.string() + " -nosidecar").has_value());
	uint16_t handle = 0;
	ASSERT_TRUE(DOS_OpenFile("D:\\README.TXT", OPEN_READ, &handle, fcb));
	UnmountHelper('D');
	EXPECT_TRUE(DriveAt('D'));
	ASSERT_TRUE(DOS_CloseFile(handle, fcb));
	UnmountHelper('D');
	EXPECT_FALSE(Drives.at(drive_index('D')));
}

TEST_F(MountArchiveTest, ATildeInTheSidecarPathIsTheHomeDirectory)
{
	const auto zip  = Built("doom.zip", Kind::ZipStored);
	const auto mine = tmp / "mine";
	std_fs::create_directories(mine);
	const auto old_home = get_env_var("HOME");
	SetEnv("HOME", tmp.string().c_str());
	const auto result = Mount("D " + zip.string() + " -sidecar ~" +
	                          CROSS_FILESPLIT + "mine");
	SetEnv("HOME", old_home.c_str());
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(UnionAt('D')->SidecarDirectory(), std_fs::canonical(mine));
}

TEST_F(MountArchiveTest, ADosPathAsSidecarPathMapsToItsHostDirectory)
{
	const auto zip  = Built("doom.zip", Kind::ZipStored);
	const auto host = tmp / "host";
	std_fs::create_directories(host / "mine");
	ASSERT_TRUE(Mount("E " + host.string()).has_value());
	ASSERT_TRUE(Mount("D " + zip.string() + " -sidecar E:\\MINE").has_value());
	EXPECT_EQ(UnionAt('D')->SidecarDirectory(), std_fs::canonical(host / "mine"));
}

TEST_F(MountArchiveTest, WithPrARelativeSidecarPathIsRelativeToTheConfig)
{
	Built("doom.zip", Kind::ZipStored);
	std_fs::create_directories(tmp / "mine");
	control->config_files.push_back((tmp / "game.conf").string());
	const auto result = Mount("D doom.zip -pr -sidecar mine");
	control->config_files.pop_back();
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(UnionAt('D')->SidecarDirectory(), std_fs::canonical(tmp / "mine"));
}

TEST_F(MountArchiveTest, AReplacedArchiveGetsANewSidecarAndTheOldIsNamed)
{
	const auto zip = Built("doom.zip", Kind::ZipStored);
	ASSERT_TRUE(Mount("D " + zip.string()).has_value());
	const auto first = UnionAt('D')->SidecarDirectory();
	UnmountHelper('D');
	// A new release under the same name.
	Built("doom.zip",
	      Kind::ZipStored,
	      {
	              {"game/", "", true},
                      {"game/doom.exe", "v2"}
        });
	ASSERT_TRUE(Mount("D " + zip.string()).has_value());
	const auto second = UnionAt('D')->SidecarDirectory();
	EXPECT_NE(first, second);
	EXPECT_TRUE(std_fs::is_directory(first));
}

TEST_F(MountArchiveTest, ASidecarBesideTheArchiveIsTakenOnlyThroughTheWhitelist)
{
	const auto zip = Built("doom.zip", Kind::ZipStored);
	// Learn the key by mounting once, then move that sidecar beside the
	// archive.
	ASSERT_TRUE(Mount("D " + zip.string()).has_value());
	const auto under_saves = UnionAt('D')->SidecarDirectory();
	UnmountHelper('D');
	const auto beside = tmp / under_saves.filename();
	std_fs::rename(under_saves, beside);
	// The test fixture's policy is owner-trusted with the webserver off:
	// the archive's directory passes, so the sidecar beside it is used.
	ASSERT_TRUE(Mount("D " + zip.string()).has_value());
	EXPECT_EQ(UnionAt('D')->SidecarDirectory(), std_fs::canonical(beside));
	UnmountHelper('D');
	// With injection possible the whitelists apply: the archive is allowed
	// as an image root, its directory is not an allowed base, so the
	// sidecar beside it is skipped and a new one is created under saves.
	const auto conf = tmp / "policy.conf";
	{
		std::ofstream out(conf);
		out << "[webserver]\nmount_allowed_image_roots = " << tmp.string()
		    << "\n";
	}
	MountPolicy::InitPolicyConfig(conf);
	MountPolicy::SetInjectionPossible(true);
	ASSERT_TRUE(Mount("D " + zip.string()).has_value());
	MountPolicy::SetInjectionPossible(false);
	MountPolicy::InitPolicyConfig({});
	ASSERT_TRUE(UnionAt('D'));
	EXPECT_EQ(UnionAt('D')->SidecarDirectory().parent_path(), tmp / "saves");
	EXPECT_TRUE(std_fs::is_directory(beside));
}

TEST_F(MountArchiveTest, StartupCleanupRemovesAnExpiredEntryWithoutAnyMount)
{
	// An entry last used eight days ago, written with the cache's own clock
	// hook; the [dos] default TTL is seven days.
	const auto zip     = Built("doom.zip", Kind::ZipStored);
	auto opened_reader = ArchiveMount::ArchiveReader::Open(zip,
	                                                       ArchiveMount::Limits{});
	ASSERT_EQ(opened_reader.error, ArchiveMount::Error::None)
	        << opened_reader.detail;
	auto source    = std::move(opened_reader.reader);
	const auto now = std::chrono::duration_cast<std::chrono::seconds>(
	                         std::chrono::system_clock::now().time_since_epoch())
	                         .count();
	ArchiveMount::CacheOptions options = {};
	options.now = [now]() { return now - 8 * 24 * 60 * 60; };
	auto cache  = ArchiveMount::ArchiveCache::Open(tmp / "cache", options);
	ASSERT_EQ(cache.status, ArchiveMount::CacheStatus::Ok) << cache.detail;
	const auto entry = ArchiveMount::ArchiveCache::EntryDirectory(
	        tmp / "cache", "doom", source->IdentityHash());
	{
		auto lease = cache.cache->Acquire(*source, zip);
		ASSERT_EQ(lease.status, ArchiveMount::CacheStatus::Ok)
		        << lease.detail;
		std_fs::path member = {};
		auto detail         = std::string();
		int readme          = -1;
		for (const auto& e : source->Entries()) {
			if (e.utf8_path == "game/readme.txt") {
				readme = e.ordinal;
			}
		}
		ASSERT_EQ(lease.lease->MemberFile(readme, member, &detail),
		          ArchiveMount::CacheStatus::Ok)
		        << detail;
	}
	cache.cache.reset();
	ASSERT_TRUE(std_fs::exists(entry));

	MOUNT::ArchiveCacheStartup();

	EXPECT_FALSE(std_fs::exists(entry));
}

TEST_F(MountArchiveTest, ChangingCacheSettingsKeepsTheStoreWhileArchivesAreMounted)
{
	// The lease behind D points at the store; a second mount after a
	// settings change must not replace it under that lease.
	Set("archive_memory_limit", "0");
	const auto a = Built("a.zip", Kind::ZipStored);
	const auto b = Built("b.zip",
	                     Kind::ZipStored,
	                     {
	                             {"x.txt", "x"}
        });
	ASSERT_TRUE(Mount("D " + a.string()));
	Set("archive_cache_max_size", "8192");
	ASSERT_TRUE(Mount("E " + b.string()));
	auto file = DriveAt('D')->FileOpen("README.TXT", OPEN_READ);
	ASSERT_TRUE(file);
	uint8_t buf[16] = {};
	uint16_t n      = sizeof(buf);
	EXPECT_TRUE(file->Read(buf, &n));
	EXPECT_EQ(std::string(reinterpret_cast<char*>(buf), n), "hello");
	file->Close();
}

TEST_F(MountArchiveTest, ReadOnlyMountsTheArchiveDriveWithoutASidecar)
{
	const auto a = Built("a.zip", Kind::ZipStored);
	ASSERT_TRUE(Mount("D " + a.string() + " -ro"));
	EXPECT_TRUE(DriveAt('D'));
	EXPECT_FALSE(UnionAt('D'));
	EXPECT_FALSE(std_fs::exists(tmp / "saves"));
}

TEST_F(MountArchiveTest, ADriveNumberWithAnExplicitArchiveTypeIsRefused)
{
	const auto a = Built("a.zip", Kind::ZipStored);
	EXPECT_FALSE(Mount("0 " + a.string() + " -t archive").has_value());
	EXPECT_FALSE(Mount("D " + a.string() + " -t archive -fs none").has_value());
	EXPECT_FALSE(Drives.at(drive_index('D')));
}

TEST_F(MountArchiveTest, ArchiveOptionsOnADirectoryMountAreRefused)
{
	const auto dir = tmp / "plain";
	std_fs::create_directories(dir);
	EXPECT_FALSE(Mount("D " + dir.string() + " -nosidecar").has_value());
	EXPECT_FALSE(Mount("D " + dir.string() + " -sidecar " + (tmp / "s").string())
	                     .has_value());
	EXPECT_FALSE(Mount("D " + dir.string() + " -root GAME").has_value());
	EXPECT_FALSE(Drives.at(drive_index('D')));
}

TEST_F(MountArchiveTest, AFloppyInsideAnArchiveMountsFromItsSidecarCopy)
{
	const auto zip = Built("images.zip", Kind::ZipDeflate, Images());
	const auto result = Mount("A " + (zip / "disks" / "disk1.img").string());
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->type, MountType::FloppyImage);
	ASSERT_NE(Drives.at(0), nullptr);
	EXPECT_NE(Drives.at(0)->GetInfoString().find(".images"), std::string::npos)
	        << Drives.at(0)->GetInfoString();
	EXPECT_STREQ(Drives.at(0)->GetLabel(), "DISKONE");
	EXPECT_EQ(ArchiveImages::HeldCountForTests(0), 1u);
	UnmountHelper('A');
	EXPECT_EQ(ArchiveImages::HeldCountForTests(0), 0u);
}

TEST_F(MountArchiveTest, AnIsoInsideAnArchiveMountsAsACdDrive)
{
	const auto zip = Built("images.zip", Kind::ZipDeflate, Images());
	ASSERT_TRUE(Mount("D " + zip.string() + "/cd/game.iso").has_value());
	EXPECT_NE(std::dynamic_pointer_cast<isoDrive>(Drives.at(drive_index('D'))),
	          nullptr);
	EXPECT_EQ(ArchiveImages::HeldCountForTests(drive_index('D')), 1u);
}

TEST_F(MountArchiveTest, ACueInsideAnArchiveMountsThroughTheRewrite)
{
	const auto zip = Built("images.zip", Kind::ZipDeflate, Images());
	ASSERT_TRUE(Mount("D " + zip.string() + "/cd/game.cue").has_value());
	EXPECT_NE(std::dynamic_pointer_cast<isoDrive>(Drives.at(drive_index('D'))),
	          nullptr);
}

TEST_F(MountArchiveTest, TwoFloppiesFromOneArchiveFormASwapSet)
{
	const auto zip = Built("images.zip", Kind::ZipDeflate, Images());
	ASSERT_TRUE(Mount("A " + zip.string() + "/disks/disk1.img " +
	                  zip.string() + "/disks/disk2.img")
	                    .has_value());
	EXPECT_EQ(ArchiveImages::HeldCountForTests(0), 2u);
}

// The cache spreads entries by stem, so zzz's copy sorts after aaa's.
TEST_F(MountArchiveTest, TheDriveModeNamesTheArchiveOfTheFirstImageTyped)
{
	auto other = Images();
	other.push_back({"other.txt", "b"});
	const auto first  = Built("zzz.zip", Kind::ZipDeflate, other);
	const auto second = Built("aaa.zip", Kind::ZipDeflate, Images());
	ASSERT_TRUE(Mount("A " + first.string() + "/disks/disk1.img " +
	                  second.string() + "/disks/disk2.img -ro")
	                    .has_value());
	const auto mode = ArchiveImages::DriveModeOf(0);
	ASSERT_TRUE(mode);
	EXPECT_EQ(mode->archive, std_fs::canonical(first));
}

TEST_F(MountArchiveTest, AnArchiveDriveAndAnImageFromItMountSideBySide)
{
	const auto zip = Built("images.zip", Kind::ZipDeflate, Images());
	ASSERT_TRUE(Mount("E " + zip.string()).has_value());
	ASSERT_NE(UnionAt('E'), nullptr);
	ASSERT_TRUE(Mount("A " + zip.string() + "/disks/disk1.img").has_value());
	EXPECT_NE(Drives.at(0), nullptr);
}

TEST_F(MountArchiveTest, AReadOnlyImageMountMakesNoSidecarCopy)
{
	const auto zip    = Built("images.zip", Kind::ZipDeflate, Images());
	const auto result = Mount("A " + zip.string() + "/disks/disk1.img -ro");
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(result->roflag);
	EXPECT_FALSE(std_fs::exists(tmp / "saves"));
}

TEST_F(MountArchiveTest, NoSidecarOnAnImageMemberMeansReadOnly)
{
	const auto zip = Built("images.zip", Kind::ZipDeflate, Images());
	const auto result = Mount("A " + zip.string() + "/disks/disk1.img -nosidecar");
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(result->roflag);
	EXPECT_FALSE(std_fs::exists(tmp / "saves"));
}

TEST_F(MountArchiveTest, RootIsRefusedWithAMemberPath)
{
	const auto zip = Built("images.zip", Kind::ZipDeflate, Images());
	EXPECT_FALSE(
	        Mount("A " + zip.string() + "/disks/disk1.img -root disks").has_value());
	EXPECT_EQ(Drives.at(0), nullptr);
	EXPECT_FALSE(std_fs::exists(tmp / "cache" / "scratch"));
}

TEST_F(MountArchiveTest, AMissingMemberIsRefusedWithItsReason)
{
	const auto zip = Built("images.zip", Kind::ZipDeflate, Images());
	EXPECT_FALSE(Mount("A " + zip.string() + "/disks/disk9.img").has_value());
	EXPECT_EQ(Drives.at(0), nullptr);
	const bool seen = std::any_of(verdicts.begin(), verdicts.end(), [](const auto& v) {
		return v.second.reason == DenyReason::ArchiveMemberNotFound;
	});
	EXPECT_TRUE(seen);
}

TEST_F(MountArchiveTest, AMemberThatIsNoDiskImageIsRefused)
{
	const auto zip = Built("images.zip", Kind::ZipDeflate, Images());
	EXPECT_FALSE(
	        Mount("A " + zip.string() + "/game/readme.txt -t floppy").has_value());
	EXPECT_EQ(Drives.at(0), nullptr);
}

TEST_F(MountArchiveTest, EveryMemberConstructionFollowsAnAllowedVerdict)
{
	const auto zip = Built("images.zip", Kind::ZipDeflate, Images());
	constructed.clear();
	ASSERT_TRUE(Mount("A " + zip.string() + "/disks/disk1.img").has_value());
	ASSERT_TRUE(Mount("D " + zip.string() + "/cd/game.cue").has_value());
	ASSERT_FALSE(constructed.empty());
	for (const auto& path : constructed) {
		std::error_code ec   = {};
		const auto canonical = std_fs::canonical(path, ec);
		const bool allowed   = std::any_of(
                        verdicts.begin(), verdicts.end(), [&](const auto& v) {
                                return v.second.allowed &&
                                       v.second.resolved == canonical;
                        });
		EXPECT_TRUE(allowed)
		        << "constructed without allowed verdict: " << path;
	}
}

TEST_F(MountArchiveTest, BootRefusesAMissingMemberAndHoldsNothing)
{
	const auto zip  = Built("images.zip", Kind::ZipDeflate, Images());
	const auto args = zip.string() + "/disks/none.img";
	auto boot       = std::make_unique<BOOT>();
	delete boot->cmd;
	boot->cmd = new CommandLine("BOOT", args);
	// Run swaps a line over 100 characters for the shell's copy.
	full_arguments = args;
	boot->Run();
	EXPECT_EQ(imageDiskList.at(0), nullptr);
	const bool seen = std::any_of(verdicts.begin(), verdicts.end(), [](const auto& v) {
		return v.second.reason == DenyReason::ArchiveMemberNotFound;
	});
	EXPECT_TRUE(seen);
}

TEST_F(MountArchiveTest, BootThroughAChainHoldsTheDiskUnderA)
{
	// 4.1: BOOT's member path walks a chain; the disk is the sidecar
	// copy and the hold lives with BOOT (KeepForBoot), under A.
	const auto inner = Built("game.zip", Kind::ZipDeflate, Images());
	const auto outer = Built("outer.zip",
	                         Kind::ZipStored,
	                         {
	                                 {"game.zip", ReadAll(inner)}
        });
	const auto args  = outer.string() + "/game.zip/disks/disk1.img";
	auto boot        = std::make_unique<BOOT>();
	delete boot->cmd;
	boot->cmd      = new CommandLine("BOOT", args);
	full_arguments = args;
	boot->Run();
	ASSERT_NE(imageDiskList.at(0), nullptr);
	EXPECT_EQ(ArchiveImages::HeldCountForTests(0), 1u);
	EXPECT_EQ(ArchiveMount::ArchiveHelperClient::LiveHelpers(), 0);
	const bool denied = std::any_of(verdicts.begin(),
	                                verdicts.end(),
	                                [](const auto& v) {
		                                return !v.second.allowed;
	                                });
	EXPECT_FALSE(denied);
}

TEST_F(MountArchiveTest, BootTakesATypedPathInCodePage437)
{
	// G13 for BOOT: the one-half sign typed as 0xAB, the folder in UTF-8.
	const auto folder = tmp / std_fs::path(u8"3½dir");
	std_fs::create_directories(folder);
	const auto zip = Built("game.zip", Kind::ZipDeflate, Images());
	std_fs::copy_file(zip, folder / "game.zip");
	const auto args = (tmp / "3").string() + "\xAB" +
	                  "dir/game.zip/disks/disk1.img";
	auto boot = std::make_unique<BOOT>();
	delete boot->cmd;
	boot->cmd      = new CommandLine("BOOT", args);
	full_arguments = args;
	boot->Run();
	EXPECT_NE(imageDiskList.at(0), nullptr);
}

TEST_F(MountArchiveTest, BootTakesATypedHostImageInCodePage437)
{
	// G13 for a plain image: converted once where it is typed, never again.
	const auto folder = tmp / std_fs::path(u8"3\u00BDdir");
	std_fs::create_directories(folder);
	std::ofstream(folder / "disk1.img", std::ios::binary) << FloppyBytes("BOOTDISK");
	const auto args = (tmp / "3").string() + "\xAB" + "dir/disk1.img";
	auto boot = std::make_unique<BOOT>();
	delete boot->cmd;
	boot->cmd      = new CommandLine("BOOT", args);
	full_arguments = args;
	boot->Run();
	EXPECT_NE(imageDiskList.at(0), nullptr);
}

// ada-adyo: a booter image named through a mounted drive, the policy judging
// the host file the drive maps it to.
TEST_F(MountArchiveTest, BootTakesAnAsciiNameThroughALocalDrive)
{
	const auto folder = tmp / "c" / "plain";
	std_fs::create_directories(folder);
	std::ofstream(folder / "disk1.img", std::ios::binary) << FloppyBytes("BOOTDISK");
	ASSERT_TRUE(Mount("C " + (tmp / "c").string()).has_value());
	const std::string args = "C:\\PLAIN\\DISK1.IMG";
	auto boot = std::make_unique<BOOT>();
	delete boot->cmd;
	boot->cmd      = new CommandLine("BOOT", args);
	full_arguments = args;
	boot->Run();
	EXPECT_NE(imageDiskList.at(0), nullptr);
}

TEST_F(MountArchiveTest, BootTakesACodePageNameThroughALocalDrive)
{
	// u-umlaut typed as 0x81 through C:, the host folder's name in UTF-8.
	const auto folder = tmp / "c" / std_fs::path(u8"f\u00FCr");
	std_fs::create_directories(folder);
	std::ofstream(folder / "disk1.img", std::ios::binary) << FloppyBytes("BOOTDISK");
	ASSERT_TRUE(Mount("C " + (tmp / "c").string()).has_value());
	const std::string args = "C:\\F\x81R\\DISK1.IMG";
	auto boot = std::make_unique<BOOT>();
	delete boot->cmd;
	boot->cmd      = new CommandLine("BOOT", args);
	full_arguments = args;
	boot->Run();
	EXPECT_NE(imageDiskList.at(0), nullptr);
}

// Plan 4b: a single-image container on the command line is its image.
TEST_F(MountArchiveTest, AnImzMountsItsFloppyFromTheSidecar)
{
	const auto imz = tmp / "game.imz";
	ASSERT_TRUE(WriteArchive(imz,
	                         Kind::ZipDeflate,
	                         {
	                                 {"DISK1.IMA", FloppyBytes("DISKONE")}
        }));
	const auto result = Mount("A " + imz.string());
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->type, MountType::FloppyImage);
	ASSERT_NE(Drives.at(0), nullptr);
	EXPECT_NE(Drives.at(0)->GetInfoString().find(".images"), std::string::npos)
	        << Drives.at(0)->GetInfoString();
	EXPECT_STREQ(Drives.at(0)->GetLabel(), "DISKONE");
	EXPECT_EQ(ArchiveImages::HeldCountForTests(0), 1u);
}

TEST_F(MountArchiveTest, ACompressedImageMountsFromTheSidecar)
{
	const auto xz = tmp / "disk1.img.xz";
	ASSERT_TRUE(WriteCompressed(xz, Filter::Xz, FloppyBytes("DISKONE")));
	const auto result = Mount("A " + xz.string());
	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->type, MountType::FloppyImage);
	ASSERT_NE(Drives.at(0), nullptr);
	EXPECT_NE(Drives.at(0)->GetInfoString().find(".images"), std::string::npos)
	        << Drives.at(0)->GetInfoString();
	EXPECT_STREQ(Drives.at(0)->GetLabel(), "DISKONE");
}

TEST_F(MountArchiveTest, AnIszMountsAsACdDrive)
{
	const auto isz = tmp / "game.isz";
	ASSERT_TRUE(WriteIsz(isz, MinimalIso(), IszChunk::Zlib));
	ASSERT_TRUE(Mount("D " + isz.string()).has_value());
	EXPECT_NE(std::dynamic_pointer_cast<isoDrive>(Drives.at(drive_index('D'))),
	          nullptr);
	EXPECT_EQ(ArchiveImages::HeldCountForTests(drive_index('D')), 1u);
}

// The command line mounts a CD container the way it mounts an ISO.
TEST_F(MountArchiveTest, ACdContainerMountsWithTypeIso)
{
	const auto isz = tmp / "game.isz";
	ASSERT_TRUE(WriteIsz(isz, MinimalIso(), IszChunk::Zlib));
	ASSERT_TRUE(Mount("D " + isz.string() + " -t iso").has_value());
	EXPECT_NE(std::dynamic_pointer_cast<isoDrive>(Drives.at(drive_index('D'))),
	          nullptr);
	const auto xz = tmp / "game.iso.xz";
	ASSERT_TRUE(WriteCompressed(xz, Filter::Xz, MinimalIso()));
	ASSERT_TRUE(Mount("E " + xz.string() + " -t iso").has_value());
	EXPECT_NE(std::dynamic_pointer_cast<isoDrive>(Drives.at(drive_index('E'))),
	          nullptr);
}

TEST_F(MountArchiveTest, AContainerAsAnArchiveDriveIsRefused)
{
	const auto imz = tmp / "game.imz";
	ASSERT_TRUE(WriteArchive(imz,
	                         Kind::ZipDeflate,
	                         {
	                                 {"DISK1.IMA", FloppyBytes("DISKONE")}
        }));
	EXPECT_FALSE(Mount("E " + imz.string() + " -t archive").has_value());
	EXPECT_EQ(Drives.at(drive_index('E')), nullptr);
	const bool seen = std::any_of(verdicts.begin(), verdicts.end(), [](const auto& v) {
		return v.second.reason == DenyReason::NotAnArchive;
	});
	EXPECT_TRUE(seen);
}

TEST_F(MountArchiveTest, RootIsRefusedOnAContainer)
{
	const auto xz = tmp / "disk1.img.xz";
	ASSERT_TRUE(WriteCompressed(xz, Filter::Xz, FloppyBytes("DISKONE")));
	EXPECT_FALSE(Mount("A " + xz.string() + " -root disks").has_value());
	EXPECT_EQ(Drives.at(0), nullptr);
}

TEST_F(MountArchiveTest, NoSidecarOnAContainerMeansReadOnly)
{
	const auto xz = tmp / "disk1.img.xz";
	ASSERT_TRUE(WriteCompressed(xz, Filter::Xz, FloppyBytes("DISKONE")));
	const auto result = Mount("A " + xz.string() + " -nosidecar");
	ASSERT_TRUE(result.has_value());
	EXPECT_TRUE(result->roflag);
	EXPECT_NE(Drives.at(0), nullptr);
	EXPECT_FALSE(std_fs::exists(tmp / "saves"));
}

TEST_F(MountArchiveTest, TheSwapTakesACompressedImage)
{
	const auto one = tmp / "disk1.img.xz";
	const auto two = tmp / "disk2.img.gz";
	ASSERT_TRUE(WriteCompressed(one, Filter::Xz, FloppyBytes("DISKONE")));
	ASSERT_TRUE(WriteCompressed(two, Filter::Gzip, FloppyBytes("DISKTWO")));
	ASSERT_TRUE(Mount("A " + one.string()).has_value());
	const auto result =
	        DriveSwap::Swap('A', two, false, {}, {std_fs::canonical(tmp)});
	ASSERT_TRUE(result.ok) << result.error;
	EXPECT_STREQ(Drives.at(0)->GetLabel(), "DISKTWO");
	EXPECT_EQ(ArchiveImages::HeldCountForTests(0), 2u);
}

TEST_F(MountArchiveTest, TheSwapTakesAMemberAndKeepsTheSetHeld)
{
	const auto zip = Built("images.zip", Kind::ZipDeflate, Images());
	ASSERT_TRUE(Mount("A " + zip.string() + "/disks/disk1.img").has_value());
	const auto result = DriveSwap::Swap('A',
	                                    zip.string() + "/disks/disk2.img",
	                                    false,
	                                    {},
	                                    {std_fs::canonical(tmp)});
	ASSERT_TRUE(result.ok) << result.error;
	EXPECT_NE(Drives.at(0)->GetInfoString().find("DISK2.IMG"), std::string::npos);
	// The label is read on mount and must be read on a swap as well.
	EXPECT_STREQ(Drives.at(0)->GetLabel(), "DISKTWO");
	EXPECT_EQ(ArchiveImages::HeldCountForTests(0), 2u);
	UnmountHelper('A');
	EXPECT_EQ(ArchiveImages::HeldCountForTests(0), 0u);
}

TEST_F(MountArchiveTest, TheSwapBackToAMemberReusesItsHold)
{
	const auto zip  = Built("images.zip", Kind::ZipDeflate, Images());
	const auto one  = zip.string() + "/disks/disk1.img";
	const auto two  = zip.string() + "/disks/disk2.img";
	const auto root = std::vector<std_fs::path>{std_fs::canonical(tmp)};
	ASSERT_TRUE(Mount("A " + one).has_value());
	ASSERT_TRUE(DriveSwap::Swap('A', two, false, {}, root).ok);
	const auto back = DriveSwap::Swap('A', one, false, {}, root);
	ASSERT_TRUE(back.ok) << back.error;
	EXPECT_STREQ(Drives.at(0)->GetLabel(), "DISKONE");
	EXPECT_EQ(ArchiveImages::HeldCountForTests(0), 2u);
	ASSERT_TRUE(DriveSwap::Swap('A', two, false, {}, root).ok);
	EXPECT_EQ(ArchiveImages::HeldCountForTests(0), 2u);
}

// The scratch directory a member mount's image lives in, from the file
// the image drive opened: <cache>/scratch/<dir>/<archive key>/.images/...
static std_fs::path ScratchOfImage(const std_fs::path& image, const std_fs::path& cache)
{
	const auto base = std_fs::weakly_canonical(cache / "scratch");
	const auto rel = std_fs::weakly_canonical(image).lexically_relative(base);
	if (rel.empty() || *rel.begin() == "..") {
		return {};
	}
	return base / *rel.begin();
}

static size_t ScratchCount(const std_fs::path& cache)
{
	size_t count       = 0;
	std::error_code ec = {};
	auto it            = std_fs::directory_iterator(cache / "scratch", ec);
	for (; !ec && it != std_fs::directory_iterator(); it.increment(ec)) {
		++count;
	}
	return count;
}

// Phase 6.4 for image members: the first disk and the swap use copies in
// the scratch; the unmount takes the scratch away.
TEST_F(MountArchiveTest, ScratchMemberMountCopiesAndSwapsInsideTheScratch)
{
	const auto zip  = Built("images.zip", Kind::ZipDeflate, Images());
	const auto root = std::vector<std_fs::path>{std_fs::canonical(tmp)};
	ASSERT_TRUE(Mount("A " + zip.string() + "/disks/disk1.img -scratch").has_value());
	ASSERT_TRUE(imageDiskList[0]);
	const auto scratch = ScratchOfImage(imageDiskList[0]->diskname, tmp / "cache");
	ASSERT_FALSE(scratch.empty()) << imageDiskList[0]->diskname;
	EXPECT_FALSE(Drives.at(0)->IsReadOnly());
	uint16_t handle = 0;
	ASSERT_TRUE(DOS_CreateFile("A:\\SAVE.DAT", {}, &handle, fcb));
	ASSERT_TRUE(DOS_CloseFile(handle, fcb));
	const auto swapped = DriveSwap::Swap(
	        'A', zip.string() + "/disks/disk2.img", false, {}, root);
	ASSERT_TRUE(swapped.ok) << swapped.error;
	EXPECT_EQ(ScratchOfImage(imageDiskList[0]->diskname, tmp / "cache"), scratch)
	        << imageDiskList[0]->diskname;
	EXPECT_FALSE(std_fs::exists(tmp / "saves"));
	UnmountHelper('A');
	EXPECT_FALSE(std_fs::exists(scratch));
	EXPECT_EQ(ArchiveImages::HeldCountForTests(0), 0u);
}

// ada-lm3d: naming an archive inside an archive as a disk refuses it
// before anything lands in a sidecar.
// Nested design 4.1: an archive-named member is entered as the drive, so
// junk under that name is "not an archive" (the image route said "not a
// disk image"); no sidecar either way.
TEST_F(MountArchiveTest, AnInnerArchiveThatIsNoArchiveIsRefusedAndLeavesNoSidecar)
{
	auto entries = Images();
	entries.push_back({"inner.zip", std::string(5000, 'z')});
	const auto zip = Built("images.zip", Kind::ZipDeflate, entries);
	EXPECT_FALSE(Mount("D " + zip.string() + "/inner.zip").has_value());
	std::error_code ec = {};
	EXPECT_TRUE(!std_fs::exists(tmp / "saves", ec) ||
	            std_fs::is_empty(tmp / "saves", ec));
	ASSERT_FALSE(verdicts.empty());
	EXPECT_EQ(verdicts.back().second.reason, DenyReason::NotAnArchive);
}

TEST_F(MountArchiveTest, ScratchMemberMountThatFailsLeavesNoScratch)
{
	const auto zip = Built("images.zip", Kind::ZipDeflate, Images());
	EXPECT_FALSE(
	        Mount("A " + zip.string() + "/disks/nothere.img -scratch").has_value());
	EXPECT_EQ(ScratchCount(tmp / "cache"), 0u);
}

// A drive number has no DOS drive whose unmount could discard a scratch,
// and its key would collide with a letter's: refused.
TEST_F(MountArchiveTest, ScratchOnADriveNumberIsRefused)
{
	const auto zip = Built("images.zip", Kind::ZipDeflate, Images());
	for (const auto& line :
	     {"0 " + zip.string() + "/disks/disk1.img -scratch",
	      "A " + zip.string() + "/disks/disk1.img -scratch -fs none"}) {
		EXPECT_FALSE(Mount(line).has_value()) << line;
		EXPECT_EQ(ScratchCount(tmp / "cache"), 0u) << line;
		EXPECT_FALSE(imageDiskList[0]) << line;
	}
}

// A -scratch line keeps every write for the mount only; a host image in
// it, or swapped onto such a drive, would be written in place.
TEST_F(MountArchiveTest, ScratchRefusesAHostImageInTheSameLine)
{
	const auto zip  = Built("images.zip", Kind::ZipDeflate, Images());
	const auto host = tmp / "save.img";
	std::ofstream(host, std::ios::binary) << FloppyBytes("SAVEDISK");
	EXPECT_FALSE(Mount("A " + zip.string() + "/disks/disk1.img " +
	                   host.string() + " -scratch")
	                     .has_value());
	EXPECT_FALSE(imageDiskList[0]);
	EXPECT_EQ(ScratchCount(tmp / "cache"), 0u);
}

TEST_F(MountArchiveTest, AHostImageCannotBeSwappedOntoAScratchDrive)
{
	const auto zip  = Built("images.zip", Kind::ZipDeflate, Images());
	const auto host = tmp / "save.img";
	std::ofstream(host, std::ios::binary) << FloppyBytes("SAVEDISK");
	const auto root = std::vector<std_fs::path>{std_fs::canonical(tmp)};
	ASSERT_TRUE(Mount("A " + zip.string() + "/disks/disk1.img -scratch").has_value());
	const auto swapped = DriveSwap::Swap('A', host.string(), false, {}, root);
	EXPECT_FALSE(swapped.ok);
	EXPECT_EQ(std::string(Drives.at(0)->GetLabel()), "DISKONE");
}

TEST_F(MountArchiveTest, TheSwapBackToACompressedImageReusesItsHold)
{
	const auto one = tmp / "disk1.img.xz";
	const auto two = tmp / "disk2.img.gz";
	ASSERT_TRUE(WriteCompressed(one, Filter::Xz, FloppyBytes("DISKONE")));
	ASSERT_TRUE(WriteCompressed(two, Filter::Gzip, FloppyBytes("DISKTWO")));
	const auto root = std::vector<std_fs::path>{std_fs::canonical(tmp)};
	ASSERT_TRUE(Mount("A " + one.string()).has_value());
	ASSERT_TRUE(DriveSwap::Swap('A', two, false, {}, root).ok);
	const auto back = DriveSwap::Swap('A', one, false, {}, root);
	ASSERT_TRUE(back.ok) << back.error;
	EXPECT_STREQ(Drives.at(0)->GetLabel(), "DISKONE");
	EXPECT_EQ(ArchiveImages::HeldCountForTests(0), 2u);
}

// Decision 1a (2026-10-08): the swap's FAT path takes the resolved
// read-only flag like the raw path, instead of a drive hard-wired
// read-only since 2026-08-01.
TEST_F(MountArchiveTest, TheSwapKeepsTheDriveWritable)
{
	const auto zip  = Built("images.zip", Kind::ZipDeflate, Images());
	const auto root = std::vector<std_fs::path>{std_fs::canonical(tmp)};
	ASSERT_TRUE(Mount("A " + zip.string() + "/disks/disk1.img").has_value());
	ASSERT_FALSE(Drives.at(0)->IsReadOnly());
	const auto result = DriveSwap::Swap(
	        'A', zip.string() + "/disks/disk2.img", false, {}, root);
	ASSERT_TRUE(result.ok) << result.error;
	EXPECT_STREQ(Drives.at(0)->GetLabel(), "DISKTWO");
	EXPECT_FALSE(Drives.at(0)->IsReadOnly());
	uint16_t handle = 0;
	ASSERT_TRUE(DOS_CreateFile("A:\\SAVE.DAT", {}, &handle, fcb));
	ASSERT_TRUE(DOS_CloseFile(handle, fcb));
}

TEST_F(MountArchiveTest, TheSwapOnAReadOnlyDriveStaysReadOnly)
{
	const auto zip  = Built("images.zip", Kind::ZipDeflate, Images());
	const auto root = std::vector<std_fs::path>{std_fs::canonical(tmp)};
	ASSERT_TRUE(Mount("A " + zip.string() + "/disks/disk1.img -ro").has_value());
	ASSERT_TRUE(Drives.at(0)->IsReadOnly());
	const auto result = DriveSwap::Swap(
	        'A', zip.string() + "/disks/disk2.img", false, {}, root);
	ASSERT_TRUE(result.ok) << result.error;
	EXPECT_STREQ(Drives.at(0)->GetLabel(), "DISKTWO");
	EXPECT_TRUE(Drives.at(0)->IsReadOnly());
	uint16_t handle = 0;
	EXPECT_FALSE(DOS_CreateFile("A:\\SAVE.DAT", {}, &handle, fcb));
}

// The hard-wired flag bit host images as much as archives.
TEST_F(MountArchiveTest, TheSwapOfAHostImageKeepsTheDriveWritable)
{
	const auto one = tmp / "one.img";
	const auto two = tmp / "two.img";
	std::ofstream(one, std::ios::binary) << FloppyBytes("DISKONE");
	std::ofstream(two, std::ios::binary) << FloppyBytes("DISKTWO");
	const auto root = std::vector<std_fs::path>{std_fs::canonical(tmp)};
	ASSERT_TRUE(Mount("A " + one.string()).has_value());
	ASSERT_FALSE(Drives.at(0)->IsReadOnly());
	const auto result = DriveSwap::Swap('A', two, false, {}, root);
	ASSERT_TRUE(result.ok) << result.error;
	EXPECT_STREQ(Drives.at(0)->GetLabel(), "DISKTWO");
	EXPECT_FALSE(Drives.at(0)->IsReadOnly());
}

// A swap back to a held disk is served from the drive's copy without
// opening the archive again: the hold carries the entry listing, so a
// sequential archive is not unpacked once more for a disk already held.
TEST_F(MountArchiveTest, TheSwapBackToAHeldMemberOpensNoArchive)
{
	const auto zip  = Built("images.zip", Kind::ZipDeflate, Images());
	const auto one  = zip.string() + "/disks/disk1.img";
	const auto two  = zip.string() + "/disks/disk2.img";
	const auto root = std::vector<std_fs::path>{std_fs::canonical(tmp)};
	ASSERT_TRUE(Mount("A " + one).has_value());
	ASSERT_TRUE(DriveSwap::Swap('A', two, false, {}, root).ok);
	// On disk the archive no longer lists disk1: an open would not
	// find the member.
	std_fs::remove(zip);
	Built("images.zip",
	      Kind::ZipDeflate,
	      {
	              {"disks/", "", true},
	              {"disks/disk2.img", FloppyBytes("DISKTWO")}
        });
	const auto back = DriveSwap::Swap('A', one, false, {}, root);
	ASSERT_TRUE(back.ok) << back.error;
	EXPECT_STREQ(Drives.at(0)->GetLabel(), "DISKONE");
	EXPECT_EQ(ArchiveImages::HeldCountForTests(0), 2u);
}

// Round 3: a drive's -ro reaches every swap, host images included; the
// old hard-wired read-only had covered these by accident.
TEST_F(MountArchiveTest, TheSwapOfAHostImageOntoAReadOnlyDriveStaysReadOnly)
{
	const auto one = tmp / "one.img";
	const auto two = tmp / "two.img";
	std::ofstream(one, std::ios::binary) << FloppyBytes("DISKONE");
	std::ofstream(two, std::ios::binary) << FloppyBytes("DISKTWO");
	const auto root = std::vector<std_fs::path>{std_fs::canonical(tmp)};
	ASSERT_TRUE(Mount("A " + one.string() + " -ro").has_value());
	ASSERT_TRUE(Drives.at(0)->IsReadOnly());
	const auto result = DriveSwap::Swap('A', two, false, {}, root);
	ASSERT_TRUE(result.ok) << result.error;
	EXPECT_STREQ(Drives.at(0)->GetLabel(), "DISKTWO");
	EXPECT_TRUE(Drives.at(0)->IsReadOnly());
	uint16_t handle = 0;
	EXPECT_FALSE(DOS_CreateFile("A:\\SAVE.DAT", {}, &handle, fcb));
}

TEST_F(MountArchiveTest, AnImageMemberMountedReadOnlySwappedToAHostImageStaysReadOnly)
{
	const auto zip = Built("images.zip", Kind::ZipDeflate, Images());
	const auto two = tmp / "two.img";
	std::ofstream(two, std::ios::binary) << FloppyBytes("DISKTWO");
	const auto root = std::vector<std_fs::path>{std_fs::canonical(tmp)};
	ASSERT_TRUE(Mount("A " + zip.string() + "/disks/disk1.img -ro").has_value());
	const auto result = DriveSwap::Swap('A', two, false, {}, root);
	ASSERT_TRUE(result.ok) << result.error;
	EXPECT_TRUE(Drives.at(0)->IsReadOnly());
}

// ada-99u7: a swap to a file the drive's Ctrl+F4 list already holds takes
// that list's object, so one file never has two writing objects with two
// FAT caches; the list's position follows the swap.
TEST_F(MountArchiveTest, ASwapToAListedImageTakesTheListsObject)
{
	const auto one = tmp / "one.img";
	const auto two = tmp / "two.img";
	std::ofstream(one, std::ios::binary) << FloppyBytes("DISKONE");
	std::ofstream(two, std::ios::binary) << FloppyBytes("DISKTWO");
	const auto root = std::vector<std_fs::path>{std_fs::canonical(tmp)};
	ASSERT_TRUE(Mount("A " + one.string() + " " + two.string()).has_value());
	const auto& listed = DriveManager::GetFilesystemImages(0);
	ASSERT_EQ(listed.size(), 2u);
	const auto result = DriveSwap::Swap('A', two.string(), false, {}, root);
	ASSERT_TRUE(result.ok) << result.error;
	EXPECT_EQ(Drives.at(0), listed[1]);
	EXPECT_STREQ(Drives.at(0)->GetLabel(), "DISKTWO");
	DriveManager::CycleDisks(0, false);
	EXPECT_EQ(Drives.at(0), listed[0]);
	EXPECT_STREQ(Drives.at(0)->GetLabel(), "DISKONE");
}

TEST_F(MountArchiveTest, AReadOnlySwapNeverTakesTheListsWritableObject)
{
	const auto one = tmp / "one.img";
	const auto two = tmp / "two.img";
	std::ofstream(one, std::ios::binary) << FloppyBytes("DISKONE");
	std::ofstream(two, std::ios::binary) << FloppyBytes("DISKTWO");
	const auto root = std::vector<std_fs::path>{std_fs::canonical(tmp)};
	ASSERT_TRUE(Mount("A " + one.string() + " " + two.string()).has_value());
	const auto& listed = DriveManager::GetFilesystemImages(0);
	ASSERT_EQ(listed.size(), 2u);
	ASSERT_FALSE(listed[1]->IsReadOnly());
	// The drive's mode turned read-only, as a slot shared with a -ro
	// mount leaves it (ada-9w06).
	auto mode = ArchiveImages::DriveModeOf(0);
	ASSERT_TRUE(mode);
	mode->read_only = true;
	ArchiveImages::SetDriveMode(0, *mode);
	const auto result = DriveSwap::Swap('A', two.string(), false, {}, root);
	ASSERT_TRUE(result.ok) << result.error;
	EXPECT_NE(Drives.at(0), listed[1]);
	EXPECT_TRUE(Drives.at(0)->IsReadOnly());
}

TEST_F(MountArchiveTest, TheRawSwapOntoAReadOnlySlotStaysReadOnly)
{
	const auto one = tmp / "one.img";
	const auto two = tmp / "two.img";
	std::ofstream(one, std::ios::binary) << FloppyBytes("DISKONE");
	std::ofstream(two, std::ios::binary) << FloppyBytes("DISKTWO");
	const auto root = std::vector<std_fs::path>{std_fs::canonical(tmp)};
	ASSERT_TRUE(Mount("0 " + one.string() + " -t floppy -fs none -ro").has_value());
	ASSERT_FALSE(Drives.at(0));
	ASSERT_TRUE(imageDiskList[0]);
	const auto result = DriveSwap::Swap('A', two, false, {}, root);
	ASSERT_TRUE(result.ok) << result.error;
	const auto before = ReadAll(two);
	auto sector       = std::array<uint8_t, 512>{};
	sector.fill(0x5A);
	EXPECT_NE(imageDiskList[0]->Write_AbsoluteSector(3, sector.data()), 0);
	EXPECT_TRUE(ReadAll(two) == before);
}

TEST_F(MountArchiveTest, AnImageAnotherDriveHoldsIsRefusedAsBusy)
{
	const auto zip = Built("images.zip", Kind::ZipDeflate, Images());
	const auto one = zip.string() + "/disks/disk1.img";
	ASSERT_TRUE(Mount("A " + one).has_value());
	const auto result =
	        DriveSwap::Swap('B', one, false, {}, {std_fs::canonical(tmp)});
	EXPECT_FALSE(result.ok);
	EXPECT_NE(result.error.find("in use by another mount"), std::string::npos)
	        << result.error;
	EXPECT_EQ(ArchiveImages::HeldCountForTests(1), 0u);
}

TEST_F(MountArchiveTest, TheSwapRefusesAMissingMemberWithTheReason)
{
	const auto zip = Built("images.zip", Kind::ZipDeflate, Images());
	ASSERT_TRUE(Mount("A " + zip.string() + "/disks/disk1.img").has_value());
	const auto result = DriveSwap::Swap('A',
	                                    zip.string() + "/disks/none.img",
	                                    false,
	                                    {},
	                                    {std_fs::canonical(tmp)});
	EXPECT_FALSE(result.ok);
	EXPECT_NE(result.error.find("no such member"), std::string::npos)
	        << result.error;
}

TEST_F(MountArchiveTest, TheSwapAppliesTheWhitelistToTheArchive)
{
	const auto zip = Built("images.zip", Kind::ZipDeflate, Images());
	ASSERT_TRUE(Mount("A " + zip.string() + "/disks/disk1.img").has_value());
	const auto elsewhere = TestTempDir::MakeUnique("swap_root_");
	const auto result    = DriveSwap::Swap(
                'A', zip.string() + "/disks/disk2.img", false, {}, {elsewhere});
	EXPECT_FALSE(result.ok);
	std::error_code ec = {};
	std_fs::remove_all(elsewhere, ec);
}

TEST_F(MountArchiveTest, ALockedEngineOpensNoArchiveForAMember)
{
	const auto zip = Built("images.zip", Kind::ZipDeflate, Images());
	MountPolicy::Lock(); // one-way: this case runs alone through ctest
	EXPECT_FALSE(Mount("A " + zip.string() + "/disks/disk1.img").has_value());
	EXPECT_FALSE(std_fs::exists(tmp / "cache"));
	EXPECT_FALSE(std_fs::exists(tmp / "saves"));
}

} // namespace

TEST_F(MountArchiveTest, AnUnsupportedMethodSaysSoAndAsksForTheArchive)
{
	auto text = std::string(MOUNT::ArchiveUnsupportedMessage);
	std::replace(text.begin(), text.end(), '\n', ' ');
	EXPECT_NE(text.find("not implemented"), std::string::npos) << text;
	EXPECT_NE(text.find("https://github.com/dosbox-automation/dosbox-automation/issues"),
	          std::string::npos)
	        << text;
	EXPECT_NE(text.find("attach the archive"), std::string::npos) << text;
	EXPECT_NE(text.find("link"), std::string::npos) << text;
}

// ada-pbr6: a path pushed the reason past 80 columns, where DOS wraps it
// mid-word. The name stands alone; every other line fits with the longest
// text the engine puts into it.
TEST(MountRefusalTexts, TheNameStandsAloneAndTheRestFits)
{
	constexpr size_t MaxColumns = 79;
	const auto longest_reason = [](const std::string_view message_id) {
		std::string longest = {};
		for (int i = 0; i < 128; ++i) {
			const auto reason = static_cast<DenyReason>(i);
			if (message_id == MountPolicy::DenyMessageId(reason)) {
				const std::string text = MountPolicy::DenyReasonText(reason);
				longest = text.size() > longest.size() ? text : longest;
			}
		}
		return longest;
	};
	std::string longest_kind = {};
	for (int i = 0; i < 64; ++i) {
		const std::string name = ArchiveMount::HeadKindName(
		        static_cast<ArchiveMount::HeadKind>(i));
		longest_kind = name.size() > longest_kind.size() ? name : longest_kind;
	}
	const std::vector<std::pair<std::string_view, std::string>> samples = {
	        {    "PROGRAM_MOUNT_POLICY_REFUSED", longest_reason("PROGRAM_MOUNT_POLICY_REFUSED")},
	        {   "PROGRAM_MOUNT_ARCHIVE_REFUSED", longest_reason("PROGRAM_MOUNT_ARCHIVE_REFUSED")},
	        {"PROGRAM_MOUNT_ARCHIVE_SEQUENTIAL", longest_kind},
	        {"PROGRAM_MOUNT_ARCHIVE_UNSUPPORTED", "PKWARE DCL implode, method 10"},
	        {      "PROGRAM_MOUNT_ARCHIVE_ROOT", "\\GAMES\\COMMANDR\\KEEN4"},
	};
	ASSERT_FALSE(MOUNT::RefusalTexts().empty());
	for (const auto& [id, text] : MOUNT::RefusalTexts()) {
		std::string sample = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
		for (const auto& [sample_id, value] : samples) {
			if (sample_id == id) {
				sample = value;
			}
		}
		auto lines = std::vector<std::string>{};
		for (const auto part : std::views::split(std::string_view(text), '\n')) {
			lines.emplace_back(part.begin(), part.end());
		}
		ASSERT_FALSE(lines.empty()) << id;
		EXPECT_EQ(lines.front(), "%s") << id;
		for (size_t n = 1; n < lines.size(); ++n) {
			auto line = lines[n];
			if (line.find_first_not_of(' ') == line.find("%s") &&
			    line.ends_with("%s")) {
				continue;
			}
			for (auto at = line.find("%s"); at != std::string::npos;
			     at = line.find("%s", at + sample.size())) {
				line.replace(at, 2, sample);
			}
			EXPECT_LE(line.size(), MaxColumns) << id << ": " << line;
		}
	}
}

TEST_F(MountArchiveTest, RunQueuesTheNamedProgram)
{
	const auto zip = Built("two.zip", Kind::ZipStored, TwoPrograms());
	ASSERT_TRUE(Mount("D " + zip.string() + " -run GAME.EXE").has_value());
	EXPECT_EQ(SHELL_TakePendingCommands(),
	          (std::vector<std::string>{"D:", "D:\\GAME.EXE"}));
}

// The menu lists a folder's game before a root setup; the one program at
// the root is still the one -run starts.
TEST_F(MountArchiveTest, RunWithoutANameStartsTheOneRootProgramBeforeFolders)
{
	const std::vector<FileEntry> entries = {
	        {"SETUP.EXE", "MZ"},
	        {"game/", "", true},
	        {"game/RUN.EXE", "MZ"},
	};
	const auto zip = Built("mixed.zip", Kind::ZipStored, entries);
	ASSERT_TRUE(Mount("D " + zip.string() + " -run").has_value());
	EXPECT_EQ(SHELL_TakePendingCommands(),
	          (std::vector<std::string>{"D:", "D:\\SETUP.EXE"}));
}

TEST_F(MountArchiveTest, RunWithoutANameFindsTheOnlyProgram)
{
	const auto zip = Built("one.zip", Kind::ZipStored, OneProgram());
	ASSERT_TRUE(Mount("D " + zip.string() + " -run").has_value());
	EXPECT_EQ(SHELL_TakePendingCommands(),
	          (std::vector<std::string>{"D:", "D:\\GAME.EXE"}));
}

TEST_F(MountArchiveTest, RunWithSeveralCandidatesQueuesUniMenu)
{
	MountBundledY();
	const auto zip = Built("two.zip", Kind::ZipStored, TwoPrograms());
	EXPECT_TRUE(Mount("D " + zip.string() + " -run").has_value());
	EXPECT_TRUE(DriveAt('D'));
	const auto queued = SHELL_TakePendingCommands();
	ASSERT_FALSE(queued.empty());
	EXPECT_EQ(queued[0], "D:");
	EXPECT_NE(queued[1].find("UNIMENU"), std::string::npos);
}

// An archive drive lists its members in the archive's order, which the
// menu must not show (Arctic Adventure listed AA2, AA3, AA1, AA4).
TEST_F(MountArchiveTest, TheMenuSortsTheProgramsOfOneKindByName)
{
	const std::vector<FileEntry> entries = {
	        {"SETUP.EXE", "MZ"},
	        {"AA2.EXE", "MZ"},
	        {"bin/", "", true},
	        {"bin/AA0.EXE", "MZ"},
	        {"aa3.exe", "MZ"},
	        {"AA1.EXE", "MZ"},
	        {"INSTALL.EXE", "MZ"},
	};
	const auto zip = Built("arctic.zip", Kind::ZipStored, entries);
	ASSERT_TRUE(Mount("D " + zip.string()).has_value());
	const auto scan = ArchiveChooser::ScanPrograms('D');
	std::vector<std::string> order = {};
	for (const auto& entry : scan.entries) {
		order.push_back(entry.exe);
	}
	EXPECT_EQ(order,
	          (std::vector<std::string>{"INSTALL.EXE",
	                                    "SETUP.EXE",
	                                    "AA1.EXE",
	                                    "AA2.EXE",
	                                    "AA3.EXE",
	                                    "BIN\\AA0.EXE"}));
}

// Decision 4 (Stefan 2026-10-05): the mount stays, nothing runs.
TEST_F(MountArchiveTest, ARunNameThatDoesNotResolveKeepsTheMount)
{
	const auto zip = Built("two.zip", Kind::ZipStored, TwoPrograms());
	EXPECT_TRUE(Mount("D " + zip.string() + " -run PLAY.EXE").has_value());
	EXPECT_TRUE(DriveAt('D'));
	EXPECT_TRUE(SHELL_TakePendingCommands().empty());
	const auto bad = Built("bad.zip", Kind::ZipStored, TwoPrograms("bad"));
	EXPECT_TRUE(Mount("E " + bad.string() + " -run ..\\GAME.EXE").has_value());
	EXPECT_TRUE(DriveAt('E'));
	EXPECT_TRUE(SHELL_TakePendingCommands().empty());
}

TEST_F(MountArchiveTest, RunIsRefusedWithFsNone)
{
	const auto img = tmp / "disk.img";
	std::ofstream(img, std::ios::binary) << FloppyWithProgram();
	const auto mounted = [] {
		return std::any_of(imageDiskList.begin(),
		                   imageDiskList.end(),
		                   [](const auto& disk) {
			                   return disk != nullptr;
		                   });
	};
	EXPECT_FALSE(Mount("D " + img.string() + " -fs none -run X.EXE").has_value());
	EXPECT_FALSE(Mount("2 " + img.string() + " -fs none -run").has_value());
	EXPECT_FALSE(mounted());
	EXPECT_TRUE(SHELL_TakePendingCommands().empty());
	// Without -run the same line mounts, so the refusal is -run's.
	EXPECT_TRUE(Mount("2 " + img.string() + " -fs none").has_value());
	EXPECT_TRUE(mounted());
}

TEST_F(MountArchiveTest, RunOnADirectoryMountWorks)
{
	const auto dir = tmp / "plain";
	std::filesystem::create_directories(dir);
	std::ofstream(dir / "GAME.EXE", std::ios::binary) << "MZ";
	ASSERT_TRUE(Mount("D " + dir.string() + " -run").has_value());
	EXPECT_EQ(SHELL_TakePendingCommands(),
	          (std::vector<std::string>{"D:", "D:\\GAME.EXE"}));
}

TEST_F(MountArchiveTest, RunOnAMemberPathFloppy)
{
	const std::vector<FileEntry> entries = {
	        {"disks/", "", true},
	        {"disks/disk1.img", FloppyWithProgram()},
	};
	const auto zip = Built("images.zip", Kind::ZipDeflate, entries);
	ASSERT_TRUE(Mount("A " + zip.string() + "/disks/disk1.img -run X").has_value());
	EXPECT_EQ(SHELL_TakePendingCommands(),
	          (std::vector<std::string>{"A:", "A:\\X.EXE"}));
}

TEST_F(MountArchiveTest, ExitIsQueuedOnlyBehindAProgram)
{
	MountBundledY();
	const auto one = Built("one.zip", Kind::ZipStored, OneProgram());
	ASSERT_TRUE(Mount("D " + one.string() + " -run GAME.EXE -exit").has_value());
	EXPECT_EQ(SHELL_TakePendingCommands(),
	          (std::vector<std::string>{"D:", "D:\\GAME.EXE", "EXIT"}));
	const auto two = Built("two.zip", Kind::ZipStored, TwoPrograms());
	ASSERT_TRUE(Mount("E " + two.string() + " -run -exit").has_value());
	// The menu is no program: EXIT waits in the session for the choice.
	EXPECT_EQ(SHELL_TakePendingCommands(),
	          (std::vector<std::string>{"E:", "Y:\\DOS\\UNIMENU.EXE"}));
	ASSERT_TRUE(MenuSession::IsOpen());
	EXPECT_EQ(MenuSession::LinesFor(0).back(), "EXIT");
	MenuSession::Close();
	const auto three = Built("three.zip", Kind::ZipStored, OneProgram("three"));
	ASSERT_TRUE(Mount("F " + three.string() + " -exit -run").has_value());
	EXPECT_EQ(SHELL_TakePendingCommands(),
	          (std::vector<std::string>{"F:", "F:\\GAME.EXE", "EXIT"}));
}

TEST_F(MountArchiveTest, ExitWithoutRunIsRefused)
{
	const auto zip = Built("one.zip", Kind::ZipStored, OneProgram());
	EXPECT_FALSE(Mount("D " + zip.string() + " -exit").has_value());
	EXPECT_FALSE(Drives.at(drive_index('D')));
	EXPECT_TRUE(SHELL_TakePendingCommands().empty());
}

TEST_F(MountArchiveTest, ASecondRunWhileOneWaitsIsRefused)
{
	ASSERT_TRUE(SHELL_QueuePendingCommands({"EARLIER"}));
	const auto zip = Built("one.zip", Kind::ZipStored, OneProgram());
	EXPECT_TRUE(Mount("D " + zip.string() + " -run GAME.EXE").has_value());
	EXPECT_TRUE(DriveAt('D'));
	EXPECT_EQ(SHELL_TakePendingCommands(),
	          (std::vector<std::string>{"EARLIER"}));
}
