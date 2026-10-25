// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "dos/programs/menu_session.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dos/dos.h"
#include "dos/drives.h"
#include "dos/programs/archive_chooser.h"
#include "dosbox_test_fixture.h"
#include "gui/osd/osd_port.h"
#include "hardware/port.h"
#include "misc/support.h"
#include "shell/shell.h"
#include "test_temp_dir.h"

namespace {

using namespace testing;
using ArchiveChooser::Category;

class MenuSessionTest : public DOSBoxTestFixture {
protected:
	void SetUp() override
	{
		DOSBoxTestFixture::SetUp();
		tmp = TestTempDir::MakeUnique("menu_session_");
		ASSERT_FALSE(tmp.empty());
		previous_drive    = Drives.at(DriveC);
		Drives.at(DriveC) = MakeDrive();
		previous_y        = Drives.at(DriveY);
		Drives.at(DriveY) = MakeDriveAt(get_resource_path("drives", "y"));
		SHELL_TakePendingCommands();
	}
	void TearDown() override
	{
		MenuSession::Close();
		SHELL_TakePendingCommands();
		Drives.at(DriveC)  = previous_drive;
		Drives.at(DriveY)  = previous_y;
		std::error_code ec = {};
		std::filesystem::remove_all(tmp, ec);
		DOSBoxTestFixture::TearDown();
	}
	std::shared_ptr<DOS_Drive> MakeDrive() const
	{
		return MakeDriveAt(tmp);
	}
	static std::shared_ptr<DOS_Drive> MakeDriveAt(const std::filesystem::path& dir)
	{
		return std::make_shared<localDrive>(
		        (dir.string() + CROSS_FILESPLIT).c_str(),
		        512,
		        32,
		        32765,
		        16000,
		        0xf8,
		        false,
		        true);
	}
	void File(const std::string& relative)
	{
		const auto path = tmp / relative;
		std::filesystem::create_directories(path.parent_path());
		std::ofstream(path, std::ios::binary) << "x";
		Drives.at(DriveC)->EmptyCache();
	}
	static ArchiveChooser::Entry Entry(const Category category,
	                                   const std::string& exe,
	                                   const std::string& args = {})
	{
		ArchiveChooser::Entry entry = {};
		entry.category              = category;
		entry.exe                   = exe;
		entry.args                  = args;
		entry.label                 = exe;
		return entry;
	}
	static ArchiveChooser::Menu Menu(std::vector<ArchiveChooser::Entry> entries)
	{
		ArchiveChooser::Menu menu = {};
		menu.title                = "Select a program";
		menu.drive                = "C";
		menu.entries              = std::move(entries);
		return menu;
	}

	static constexpr uint8_t DriveC           = 2;
	static constexpr uint8_t DriveY           = 24;
	std::filesystem::path tmp                 = {};
	std::shared_ptr<DOS_Drive> previous_drive = {};
	std::shared_ptr<DOS_Drive> previous_y     = {};
};

TEST_F(MenuSessionTest, AGameInAFolderRunsTheWayMountRunWould)
{
	File("PLAY.BAT");
	File("KEEN/RUN.BAT");
	MenuSession::Open('C',
	                  Menu({Entry(Category::Game, "PLAY.BAT"),
	                        Entry(Category::Game, "KEEN\\RUN.BAT")}));
	EXPECT_THAT(MenuSession::LinesFor(1),
	            ElementsAre("C:", "CD \\KEEN", "CALL C:\\KEEN\\RUN.BAT"));
	EXPECT_THAT(MenuSession::LinesFor(0), ElementsAre("C:", "CALL C:\\PLAY.BAT"));
}

TEST_F(MenuSessionTest, ASetupEntryGoesBackToTheMenu)
{
	File("SETUP/SETSOUND.EXE");
	MenuSession::Open('C',
	                  Menu({Entry(Category::Setup, "SETUP\\SETSOUND.EXE")}));
	EXPECT_THAT(MenuSession::LinesFor(0),
	            ElementsAre("C:",
	                        "CD \\SETUP",
	                        "C:\\SETUP\\SETSOUND.EXE",
	                        "CD \\",
	                        "Y:\\DOS\\UNIMENU.EXE"));
}

// Without the bundled UniMenu a setup entry just ends; it never gets the
// EXIT of a final choice.
TEST_F(MenuSessionTest, WithoutTheMenuProgramASetupEntryOnlyRuns)
{
	File("SETUP/SETSOUND.EXE");
	MenuSession::Open('C',
	                  Menu({Entry(Category::Setup, "SETUP\\SETSOUND.EXE")}),
	                  true);
	Drives.at(DriveY) = nullptr;
	EXPECT_THAT(MenuSession::LinesFor(0),
	            ::testing::ElementsAre("C:", "CD \\SETUP", "C:\\SETUP\\SETSOUND.EXE"));
}

// Nothing comes back to a session without UniMenu, so a setup choice
// closes it instead of leaving the port answering for a stale menu.
TEST_F(MenuSessionTest, ASetupChoiceWithoutTheMenuProgramClosesTheSession)
{
	File("SETUP/SETSOUND.EXE");
	MenuSession::Open('C',
	                  Menu({Entry(Category::Setup, "SETUP\\SETSOUND.EXE")}));
	Drives.at(DriveY) = nullptr;
	EXPECT_TRUE(MenuSession::Choose(0));
	EXPECT_FALSE(MenuSession::IsOpen());
}

// MOUNT -run -exit: the emulator ends after the chosen program, and
// stays when the menu is cancelled or a setup entry comes back.
TEST_F(MenuSessionTest, AFinalChoiceEndsWithExitWhenTheMountAskedForIt)
{
	File("GAME.EXE");
	File("SETUP.EXE");
	MenuSession::Open('C',
	                  Menu({Entry(Category::Game, "GAME.EXE"),
	                        Entry(Category::Setup, "SETUP.EXE")}),
	                  true);
	EXPECT_THAT(MenuSession::LinesFor(0),
	            ElementsAre("C:", "C:\\GAME.EXE", "EXIT"));
	EXPECT_EQ(MenuSession::LinesFor(1).back(), "Y:\\DOS\\UNIMENU.EXE");
}

TEST_F(MenuSessionTest, ArgumentsFollowTheProgram)
{
	File("INSTALLH.BAT");
	MenuSession::Open('C',
	                  Menu({Entry(Category::Setup, "INSTALLH.BAT", "C /S")}));
	EXPECT_THAT(MenuSession::LinesFor(0),
	            ElementsAre("C:", "CALL C:\\INSTALLH.BAT C /S", "CD \\", "Y:\\DOS\\UNIMENU.EXE"));
}

TEST_F(MenuSessionTest, ArgumentsWithShellMetacharactersAreRefused)
{
	File("GAME.EXE");
	for (const auto* args :
	     {"> NUL", "| MORE", "& DEL *.*", "%PATH%", "A;B", "A < B", "LINE\nBREAK"}) {
		MenuSession::Open('C',
		                  Menu({Entry(Category::Game, "GAME.EXE", args)}));
		EXPECT_TRUE(MenuSession::LinesFor(0).empty()) << args;
	}
}

TEST_F(MenuSessionTest, ANumberPastTheEntriesRunsNothing)
{
	File("GAME.EXE");
	MenuSession::Open('C', Menu({Entry(Category::Game, "GAME.EXE")}));
	EXPECT_TRUE(MenuSession::LinesFor(1).empty());
	EXPECT_FALSE(MenuSession::Choose(1));
	EXPECT_TRUE(SHELL_TakePendingCommands().empty());
	EXPECT_TRUE(MenuSession::IsOpen());
}

TEST_F(MenuSessionTest, WithoutASessionNothingRuns)
{
	File("GAME.EXE");
	EXPECT_FALSE(MenuSession::IsOpen());
	EXPECT_TRUE(MenuSession::LinesFor(0).empty());
	EXPECT_FALSE(MenuSession::Choose(0));
	EXPECT_TRUE(SHELL_TakePendingCommands().empty());
}

TEST_F(MenuSessionTest, AnEntryWhoseFileIsGoneRunsNothing)
{
	MenuSession::Open('C', Menu({Entry(Category::Game, "GONE.EXE")}));
	EXPECT_TRUE(MenuSession::LinesFor(0).empty());
}

TEST_F(MenuSessionTest, AnEntryNotOnTheDriveRunsNothing)
{
	File("GAME.EXE");
	for (const auto* exe : {"..\\GAME.EXE", "Z:\\MOUNT.COM", "README.TXT"}) {
		MenuSession::Open('C', Menu({Entry(Category::Game, exe)}));
		EXPECT_TRUE(MenuSession::LinesFor(0).empty()) << exe;
	}
}

TEST_F(MenuSessionTest, AGameChoiceQueuesItsLinesAndClosesTheSession)
{
	File("KEEN/RUN.BAT");
	MenuSession::Open('C', Menu({Entry(Category::Game, "KEEN\\RUN.BAT")}));
	const auto expected = MenuSession::LinesFor(0);
	ASSERT_FALSE(expected.empty());
	EXPECT_TRUE(MenuSession::Choose(0));
	EXPECT_EQ(SHELL_TakePendingCommands(), expected);
	EXPECT_FALSE(MenuSession::IsOpen());
}

TEST_F(MenuSessionTest, ASetupChoiceKeepsTheSessionForTheReturn)
{
	File("SETUP.EXE");
	MenuSession::Open('C', Menu({Entry(Category::Setup, "SETUP.EXE")}));
	EXPECT_TRUE(MenuSession::Choose(0));
	EXPECT_FALSE(SHELL_TakePendingCommands().empty());
	EXPECT_TRUE(MenuSession::IsOpen());
}

TEST_F(MenuSessionTest, AReplacedDriveClosesTheSession)
{
	File("GAME.EXE");
	MenuSession::Open('C', Menu({Entry(Category::Game, "GAME.EXE")}));
	ASSERT_TRUE(MenuSession::IsOpen());
	Drives.at(DriveC) = MakeDrive();
	EXPECT_FALSE(MenuSession::IsOpen());
	EXPECT_TRUE(MenuSession::LinesFor(0).empty());
	EXPECT_FALSE(MenuSession::NextByte());
}

// MOUNT -u, then a new MOUNT: the new drive may get the freed drive's
// address, which must not count as the same drive. Placement new puts
// both in one storage so the address is reused for certain.
TEST_F(MenuSessionTest, ANewDriveAtTheFreedAddressClosesTheSession)
{
	File("GAME.EXE");
	// A localDrive is over 1 MiB, more than a Windows stack holds.
	struct FreeAligned {
		void operator()(std::byte* p) const
		{
			::operator delete(p, std::align_val_t{alignof(localDrive)});
		}
	};
	const std::unique_ptr<std::byte, FreeAligned> storage(static_cast<std::byte*>(
	        ::operator new(sizeof(localDrive),
	                       std::align_val_t{alignof(localDrive)})));
	const auto in_storage = [&]() {
		auto* drive = new (storage.get())
		        localDrive((tmp.string() + CROSS_FILESPLIT).c_str(),
		                   512,
		                   32,
		                   32765,
		                   16000,
		                   0xf8,
		                   false,
		                   true);
		return std::shared_ptr<DOS_Drive>(drive, [](DOS_Drive* d) {
			std::destroy_at(d);
		});
	};
	// Drives[C] must let go of the storage on every way out of the test.
	struct Release {
		std::shared_ptr<DOS_Drive>& slot;
		~Release()
		{
			slot = nullptr;
		}
	} release{Drives.at(DriveC)};

	Drives.at(DriveC) = in_storage();
	MenuSession::Open('C', Menu({Entry(Category::Game, "GAME.EXE")}));
	ASSERT_TRUE(MenuSession::IsOpen());

	Drives.at(DriveC) = nullptr;
	Drives.at(DriveC) = in_storage();

	EXPECT_FALSE(MenuSession::IsOpen());
}

TEST_F(MenuSessionTest, AnUnmountedDriveClosesTheSession)
{
	File("GAME.EXE");
	MenuSession::Open('C', Menu({Entry(Category::Game, "GAME.EXE")}));
	Drives.at(DriveC) = nullptr;
	EXPECT_FALSE(MenuSession::IsOpen());
	EXPECT_FALSE(MenuSession::Choose(0));
}

TEST_F(MenuSessionTest, TheMenuProgramNeedsTheBundledYDrive)
{
	const auto y          = drive_index('Y');
	const auto previous_y = Drives.at(y);
	const auto mount_y_at = [&](const std::string& dir) {
		Drives.at(y) = std::make_shared<localDrive>(
		        (dir + CROSS_FILESPLIT).c_str(), 512, 32, 32765, 16000, 0xf8, false, true);
	};

	Drives.at(y) = nullptr;
	EXPECT_FALSE(MenuSession::MenuProgramAvailable());

	mount_y_at(tmp.string());
	EXPECT_FALSE(MenuSession::MenuProgramAvailable());

	mount_y_at(get_resource_path("drives", "y").string());
	EXPECT_TRUE(MenuSession::MenuProgramAvailable());

	Drives.at(y) = previous_y;
}

TEST_F(MenuSessionTest, TheDataStreamIsTheSerializedMenu)
{
	File("GAME.EXE");
	const auto menu = Menu({Entry(Category::Game, "GAME.EXE")});
	MenuSession::Open('C', menu);
	const auto expected = ArchiveChooser::SerializeMenu(menu);
	for (int pass = 0; pass < 2; ++pass) {
		MenuSession::Rewind();
		std::string streamed = {};
		while (const auto byte = MenuSession::NextByte()) {
			streamed.push_back(static_cast<char>(*byte));
			ASSERT_LE(streamed.size(), expected.size());
		}
		EXPECT_EQ(streamed, expected) << "pass " << pass;
		EXPECT_EQ(MenuSession::Remaining(), 0u);
	}
	MenuSession::Rewind();
	EXPECT_EQ(MenuSession::Remaining(), expected.size());
}

TEST_F(MenuSessionTest, CloseEndsTheStream)
{
	File("GAME.EXE");
	MenuSession::Open('C', Menu({Entry(Category::Game, "GAME.EXE")}));
	MenuSession::Close();
	MenuSession::Rewind();
	EXPECT_FALSE(MenuSession::NextByte());
	EXPECT_FALSE(MenuSession::IsOpen());
}

// --- The menu commands on ports 0x3E0/0x3E1 ----------------------------------

class MenuPortTest : public MenuSessionTest {
protected:
	void SetUp() override
	{
		MenuSessionTest::SetUp();
		OSDPORT_Init();
	}
	void TearDown() override
	{
		OSDPORT_Destroy();
		MenuSessionTest::TearDown();
	}
	static void Send(std::initializer_list<uint8_t> data, const uint8_t commit)
	{
		IO_WriteB(PortCommand, 0x10);
		for (const auto byte : data) {
			IO_WriteB(PortData, byte);
		}
		IO_WriteB(PortCommand, commit);
	}

	static constexpr io_port_t PortCommand = 0x3e0;
	static constexpr io_port_t PortData    = 0x3e1;
};

TEST_F(MenuPortTest, WithoutSessionOrOverlayThePortLooksUnclaimed)
{
	EXPECT_EQ(IO_ReadB(PortCommand), 0xff);
}

TEST_F(MenuPortTest, AnOpenSessionAnswersTheSignature)
{
	File("GAME.EXE");
	MenuSession::Open('C', Menu({Entry(Category::Game, "GAME.EXE")}));
	EXPECT_EQ(IO_ReadB(PortCommand), 0x4f);
}

// UniMenu typed at the prompt with no menu waiting hung in its read loop.
TEST_F(MenuPortTest, ATransferWithoutSessionEndsAtOnce)
{
	IO_WriteB(PortCommand, 0x13);
	EXPECT_EQ(IO_ReadB(PortCommand), 0x14);
}

TEST_F(MenuPortTest, ATransferServesTheMenuThenEnds)
{
	File("GAME.EXE");
	const auto menu = Menu({Entry(Category::Game, "GAME.EXE")});
	MenuSession::Open('C', menu);
	const auto expected = ArchiveChooser::SerializeMenu(menu);

	IO_WriteB(PortCommand, 0x13);
	std::string streamed = {};
	auto status          = IO_ReadB(PortCommand);
	while (status == 0x4f && streamed.size() <= expected.size()) {
		streamed.push_back(static_cast<char>(IO_ReadB(PortData)));
		status = IO_ReadB(PortCommand);
	}
	EXPECT_EQ(status, 0x14);
	EXPECT_EQ(streamed, expected);
}

TEST_F(MenuPortTest, AnEntryNumberThroughThePortsQueuesItsLines)
{
	File("PLAY.BAT");
	File("KEEN/RUN.BAT");
	MenuSession::Open('C',
	                  Menu({Entry(Category::Game, "PLAY.BAT"),
	                        Entry(Category::Game, "KEEN\\RUN.BAT")}));
	const auto expected = MenuSession::LinesFor(1);
	ASSERT_FALSE(expected.empty());
	Send({1}, 0x11);
	EXPECT_EQ(SHELL_TakePendingCommands(), expected);
	EXPECT_FALSE(MenuSession::IsOpen());
}

TEST_F(MenuPortTest, TheReturnCommandIsACommitTheEngineDecidesTheReturn)
{
	File("GAME.EXE");
	MenuSession::Open('C', Menu({Entry(Category::Game, "GAME.EXE")}));
	Send({0}, 0x15);
	EXPECT_THAT(SHELL_TakePendingCommands(), ElementsAre("C:", "C:\\GAME.EXE"));
}

TEST_F(MenuPortTest, AChoiceOutsideASessionQueuesNothing)
{
	File("GAME.EXE");
	Send({0}, 0x11);
	EXPECT_TRUE(SHELL_TakePendingCommands().empty());
}

TEST_F(MenuPortTest, CommandTextInsteadOfANumberQueuesNothing)
{
	File("GAME.EXE");
	MenuSession::Open('C', Menu({Entry(Category::Game, "GAME.EXE")}));
	Send({'D', 'E', 'L', ' ', '*', '.', '*'}, 0x11);
	Send({}, 0x11);
	EXPECT_TRUE(SHELL_TakePendingCommands().empty());
	EXPECT_TRUE(MenuSession::IsOpen());
}

TEST_F(MenuPortTest, ACommitWithoutBeginQueuesNothing)
{
	File("GAME.EXE");
	MenuSession::Open('C', Menu({Entry(Category::Game, "GAME.EXE")}));
	IO_WriteB(PortData, 0);
	IO_WriteB(PortCommand, 0x11);
	EXPECT_TRUE(SHELL_TakePendingCommands().empty());
}

TEST_F(MenuPortTest, CancelClosesTheSession)
{
	File("GAME.EXE");
	MenuSession::Open('C', Menu({Entry(Category::Game, "GAME.EXE")}));
	IO_WriteB(PortCommand, 0x12);
	EXPECT_FALSE(MenuSession::IsOpen());
	EXPECT_EQ(IO_ReadB(PortCommand), 0xff);
}

} // namespace
