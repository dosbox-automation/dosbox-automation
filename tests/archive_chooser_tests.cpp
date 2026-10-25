// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/programs/archive_chooser.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "dos/dos.h"
#include "dos/drives.h"
#include "dosbox_test_fixture.h"
#include "test_temp_dir.h"
#include "utils/checks.h"

CHECK_NARROWING();

namespace {

using namespace ArchiveChooser;

// -- Pure function tests (no DOSBox fixture needed) --

TEST(ArchiveChooserSanitize, StripsControlChars)
{
	EXPECT_EQ(SanitizeLabel("Hello\x01World"), "HelloWorld");
	EXPECT_EQ(SanitizeLabel("Normal text"), "Normal text");
	EXPECT_EQ(SanitizeLabel("\x7f"
	                        "DEL"),
	          "DEL");
}

TEST(ArchiveChooserSanitize, TruncatesAt60)
{
	const std::string long_label(80, 'A');
	EXPECT_EQ(SanitizeLabel(long_label).size(), 60u);
}

TEST(ArchiveChooserSanitize, EmptyInput)
{
	EXPECT_EQ(SanitizeLabel(""), "");
}

TEST(ArchiveChooserValidPath, AcceptsSimpleName)
{
	EXPECT_TRUE(IsValidExePath("DOOM.EXE"));
	EXPECT_TRUE(IsValidExePath("GAME\\RUN.BAT"));
	EXPECT_TRUE(IsValidExePath("\\GAME\\RUN.BAT"));
}

TEST(ArchiveChooserValidPath, RejectsMetachars)
{
	EXPECT_FALSE(IsValidExePath("DOOM.EXE>NUL"));
	EXPECT_FALSE(IsValidExePath("RUN|HACK"));
	EXPECT_FALSE(IsValidExePath("A&B.EXE"));
	EXPECT_FALSE(IsValidExePath("A;B.EXE"));
	EXPECT_FALSE(IsValidExePath("A%B.EXE"));
	EXPECT_FALSE(IsValidExePath("A<B.EXE"));
}

TEST(ArchiveChooserValidPath, RejectsDirectoryEscape)
{
	EXPECT_FALSE(IsValidExePath("..\\GAME.EXE"));
	EXPECT_FALSE(IsValidExePath("GAME\\..\\SECRET.EXE"));
}

TEST(ArchiveChooserValidPath, RejectsLongComponents)
{
	EXPECT_FALSE(IsValidExePath("LONGFILENAME.EXE"));
	EXPECT_FALSE(IsValidExePath("SHORT.LONGEXT"));
}

TEST(ArchiveChooserValidPath, RejectsEmpty)
{
	EXPECT_FALSE(IsValidExePath(""));
}

TEST(ArchiveChooserValidPath, RejectsControlChars)
{
	EXPECT_FALSE(IsValidExePath("A\x01.EXE"));
}

// The rule Resolve applies when the entry is chosen, so the menu never
// offers an entry that cannot start.
TEST(ArchiveChooserValidPath, RejectsWhatResolveWouldRefuse)
{
	EXPECT_FALSE(IsValidExePath("MY GAME.EXE"));
	EXPECT_FALSE(IsValidExePath("\"X.EXE"));
	EXPECT_FALSE(IsValidExePath("C:\\GAME.EXE"));
	EXPECT_FALSE(IsValidExePath("-GAME.EXE"));
	EXPECT_FALSE(IsValidExePath("GAME.E+E"));
	EXPECT_FALSE(IsValidExePath("A\x7f.EXE"));
}

TEST(ArchiveChooserValidPath, RejectsFilesThatAreNotPrograms)
{
	EXPECT_FALSE(IsValidExePath("README.TXT"));
	EXPECT_FALSE(IsValidExePath("GAME\\GAME.DAT"));
}

TEST(ArchiveChooserValidPath, AcceptsANameWithoutExtensionAndLowerCase)
{
	EXPECT_TRUE(IsValidExePath("GAME"));
	EXPECT_TRUE(IsValidExePath("game\\run.bat"));
}

TEST(ArchiveChooserSerialize, RoundTripsCorrectly)
{
	Menu menu  = {};
	menu.title = "Test Menu";
	menu.drive = "C";

	Entry e1    = {};
	e1.category = Category::Game;
	e1.exe      = "DOOM.EXE";
	e1.label    = "DOOM";

	Entry e2    = {};
	e2.category = Category::Setup;
	e2.exe      = "SETUP.EXE";
	e2.args     = "/S";
	e2.label    = "Setup";

	menu.entries = {e1, e2};

	const auto text = SerializeMenu(menu);
	EXPECT_NE(text.find("Test Menu"), std::string::npos);
	EXPECT_NE(text.find("[GAME]"), std::string::npos);
	EXPECT_NE(text.find("DOOM.EXE"), std::string::npos);
	EXPECT_NE(text.find("DOOM"), std::string::npos);
	EXPECT_NE(text.find("[SETUP]"), std::string::npos);
	EXPECT_NE(text.find("SETUP.EXE /S"), std::string::npos);
}

TEST(ArchiveChooserToml, ValidTomlProducesMenu)
{
	const auto toml = R"(
[[entry]]
label = "Doom"
exe = "DOOM.EXE"
type = "game"

[[entry]]
label = "Setup Sound"
exe = "SETUP.EXE"
type = "setup"
)";
	auto menu       = ParseChooserToml(toml, "My Game");
	ASSERT_TRUE(menu.has_value());
	EXPECT_EQ(menu->title, "My Game");
	EXPECT_EQ(menu->entries.size(), 2u);
	EXPECT_EQ(menu->entries[0].label, "Doom");
	EXPECT_EQ(menu->entries[0].exe, "DOOM.EXE");
	EXPECT_EQ(menu->entries[0].category, Category::Game);
	EXPECT_EQ(menu->entries[1].category, Category::Setup);
}

// The author's order is the menu's: no sort by kind or by name.
TEST(ArchiveChooserToml, EntriesKeepTheFileOrder)
{
	const auto toml = R"(
[[entry]]
label = "Zeta setup"
exe = "ZSETUP.EXE"
type = "setup"

[[entry]]
label = "Alpha"
exe = "ALPHA.EXE"
type = "game"
)";
	const auto menu = ParseChooserToml(toml, "Order");
	ASSERT_TRUE(menu.has_value());
	ASSERT_EQ(menu->entries.size(), 2u);
	EXPECT_EQ(menu->entries[0].exe, "ZSETUP.EXE");
	EXPECT_EQ(menu->entries[1].exe, "ALPHA.EXE");
}

// The marked entry is the one the engine runs without showing the menu.
TEST(ArchiveChooserToml, AutostartMarksItsEntry)
{
	const auto toml = R"(
[[entry]]
label = "Setup"
exe = "SETUP.EXE"
type = "setup"

[[entry]]
label = "Game"
exe = "GAME.EXE"
autostart = true
)";
	const auto menu = ParseChooserToml(toml, "Title");
	ASSERT_TRUE(menu.has_value());
	ASSERT_EQ(menu->entries.size(), 2u);
	ASSERT_TRUE(menu->autostart.has_value());
	EXPECT_EQ(*menu->autostart, 1u);
	EXPECT_FALSE(ParseChooserToml("[[entry]]\nlabel = \"G\"\nexe = \"G.EXE\"\n", "T")
	                     ->autostart.has_value());
}

TEST(ArchiveChooserToml, TheFileTitleWinsAndIsSanitized)
{
	const auto toml = "title = \"Commander\tKeen 4\"\n[[entry]]\nlabel = \"G\"\nexe = \"G.EXE\"\n";
	const auto menu = ParseChooserToml(toml, "Fallback");
	ASSERT_TRUE(menu.has_value());
	EXPECT_EQ(menu->title, "CommanderKeen 4");
}

TEST(ArchiveChooserToml, UnsafeOrLongArgumentsDropTheEntry)
{
	const std::string long_args(41, 'A');
	for (const auto& args : {std::string("> NUL"),
	                         std::string("A|B"),
	                         std::string("%PATH%"),
	                         std::string("A;B"),
	                         long_args}) {
		const auto toml =
		        "[[entry]]\nlabel = \"Good\"\nexe = \"GOOD.EXE\"\nargs = \"C /S\"\n"
		        "[[entry]]\nlabel = \"Bad\"\nexe = \"BAD.EXE\"\nargs = \"" +
		        args + "\"\n";
		const auto menu = ParseChooserToml(toml, "T");
		ASSERT_TRUE(menu.has_value()) << args;
		ASSERT_EQ(menu->entries.size(), 1u) << args;
		EXPECT_EQ(menu->entries[0].args, "C /S");
	}
}

TEST(ArchiveChooserToml, InvalidExePathIsSkipped)
{
	const auto toml = R"(
[[entry]]
label = "Good"
exe = "GAME.EXE"

[[entry]]
label = "Bad"
exe = "..\\ESCAPE.EXE"
)";
	auto menu       = ParseChooserToml(toml, "Title");
	ASSERT_TRUE(menu.has_value());
	EXPECT_EQ(menu->entries.size(), 1u);
	EXPECT_EQ(menu->entries[0].exe, "GAME.EXE");
}

TEST(ArchiveChooserToml, LabelIsSanitized)
{
	// TOML basic strings process \t but not \x01, so test with a tab
	// which is a valid TOML escape but a control char for the label.
	const auto toml = R"(
[[entry]]
label = "My\tGame"
exe = "GAME.EXE"
)";
	auto menu       = ParseChooserToml(toml, "Title");
	ASSERT_TRUE(menu.has_value());
	ASSERT_EQ(menu->entries.size(), 1u);
	EXPECT_EQ(menu->entries[0].label.find('\t'), std::string::npos);
}

TEST(ArchiveChooserToml, EmptyTableArrayReturnsNullopt)
{
	EXPECT_FALSE(ParseChooserToml("", "Title").has_value());
	EXPECT_FALSE(ParseChooserToml("[root]\nkey = \"val\"", "Title").has_value());
}

TEST(ArchiveChooserToml, MissingRequiredFieldsSkipsEntry)
{
	const auto toml = R"(
[[entry]]
exe = "GAME.EXE"

[[entry]]
label = "No Exe"
)";
	EXPECT_FALSE(ParseChooserToml(toml, "Title").has_value());
}

TEST(ArchiveChooserToml, CapsAt20Entries)
{
	std::string toml = {};
	for (int i = 0; i < 25; ++i) {
		toml += "[[entry]]\nlabel = \"E" + std::to_string(i) +
		        "\"\nexe = \"E" + std::to_string(i) + ".EXE\"\n\n";
	}
	auto menu = ParseChooserToml(toml, "Title");
	ASSERT_TRUE(menu.has_value());
	EXPECT_EQ(menu->entries.size(), 20u);
}

// -- ScanPrograms tests (need DOSBoxTestFixture with a mounted drive) --

class ArchiveChooserScanTest : public DOSBoxTestFixture {
protected:
	void SetUp() override
	{
		DOSBoxTestFixture::SetUp();
		tmp = TestTempDir::MakeUnique("chooser_");
		ASSERT_FALSE(tmp.empty());
		previous_drive    = Drives.at(DriveC);
		Drives.at(DriveC) = std::make_shared<localDrive>(
		        (tmp.string() + CROSS_FILESPLIT).c_str(),
		        512,
		        32,
		        32765,
		        16000,
		        0xf8,
		        false,
		        true);
	}
	void TearDown() override
	{
		Drives.at(DriveC)  = previous_drive;
		std::error_code ec = {};
		std::filesystem::remove_all(tmp, ec);
		DOSBoxTestFixture::TearDown();
	}
	void File(const std::string& relative, const std::string& content = "x")
	{
		const auto path = tmp / relative;
		std::filesystem::create_directories(path.parent_path());
		std::ofstream(path, std::ios::binary) << content;
		Drives.at(DriveC)->EmptyCache();
	}

	static constexpr uint8_t DriveC           = 2;
	std::filesystem::path tmp                 = {};
	std::shared_ptr<DOS_Drive> previous_drive = {};
};

TEST_F(ArchiveChooserScanTest, ASingleExeIsOneEntry)
{
	File("KEEN4E.EXE");
	EXPECT_EQ(ScanPrograms('C').entries.size(), 1u);
}

TEST_F(ArchiveChooserScanTest, TwoExesProducesMenu)
{
	File("DOOM.EXE");
	File("SETUP.EXE");
	const auto menu = ProgramMenu('c', ScanPrograms('C').entries);
	EXPECT_EQ(menu.entries.size(), 2u);
	EXPECT_EQ(menu.drive, "C");
}

TEST_F(ArchiveChooserScanTest, PatternClassification)
{
	File("RUN.BAT");
	File("INSTALL.EXE");
	File("MISC.COM");
	const auto menu = ScanPrograms('C');
	ASSERT_EQ(menu.entries.size(), 3u);
	EXPECT_EQ(menu.entries[0].category, Category::Game);
	EXPECT_EQ(menu.entries[0].exe, "RUN.BAT");
	EXPECT_EQ(menu.entries[1].category, Category::Setup);
	EXPECT_EQ(menu.entries[1].exe, "INSTALL.EXE");
	EXPECT_EQ(menu.entries[2].category, Category::Other);
}

// A name that contains a setup word anywhere is a setup program.
TEST_F(ArchiveChooserScanTest, NamesContainingASetupWordAreSetup)
{
	File("PLAY.EXE");
	for (const auto* name :
	     {"MYSOUND.EXE", "SOUND.COM", "XCFG.EXE", "HWDIAG.EXE", "BUGPATCH.BAT"}) {
		File(name);
	}
	const auto menu = ScanPrograms('C');
	ASSERT_EQ(menu.entries.size(), 6u);
	EXPECT_EQ(menu.entries[0].exe, "PLAY.EXE");
	for (size_t i = 1; i < menu.entries.size(); ++i) {
		EXPECT_EQ(menu.entries[i].category, Category::Setup)
		        << menu.entries[i].exe;
	}
}

// Subfolders are scanned after the root, so the game below is the last
// program found; the cap must not cut it.
TEST_F(ArchiveChooserScanTest, TheCapKeepsTheGameOfALargeDrive)
{
	for (int i = 0; i < 59; ++i) {
		File("PROG" + std::to_string(i) + ".EXE");
	}
	File("ZZ/RUN.EXE");
	const auto menu = ScanPrograms('C');
	EXPECT_EQ(menu.entries.size(), 50u);
	EXPECT_EQ(menu.entries.front().exe, "ZZ\\RUN.EXE");
	EXPECT_EQ(menu.entries.front().category, Category::Game);
}

// CHOOSER.TML: the 8.3 name, read through the drive like any file.
TEST_F(ArchiveChooserScanTest, ReadChooserReadsTheFileAtTheRoot)
{
	File("GAME.EXE");
	File("CHOOSER.TML",
	     "title = \"Curated\"\n[[entry]]\nlabel = \"Play\"\nexe = \"GAME.EXE\"\n");
	const auto menu = ReadChooser('c');
	ASSERT_TRUE(menu.has_value());
	EXPECT_EQ(menu->title, "Curated");
	EXPECT_EQ(menu->drive, "C");
	ASSERT_EQ(menu->entries.size(), 1u);
	EXPECT_EQ(menu->entries[0].label, "Play");
}

TEST_F(ArchiveChooserScanTest, ReadChooserWithoutTheFileGivesNothing)
{
	File("GAME.EXE");
	EXPECT_FALSE(ReadChooser('C').has_value());
	EXPECT_FALSE(ReadChooser('1').has_value());
}

TEST_F(ArchiveChooserScanTest, AnOversizedChooserIsIgnored)
{
	std::string toml = "[[entry]]\nlabel = \"Play\"\nexe = \"GAME.EXE\"\n";
	toml += "# " + std::string(17 * 1024, 'x') + "\n";
	File("CHOOSER.TML", toml);
	EXPECT_FALSE(ReadChooser('C').has_value());
}

TEST_F(ArchiveChooserScanTest, UnderscoreFilesAreSkipped)
{
	File("_INSTALL.BAT");
	File("GAME.EXE");
	File("MISC.EXE");
	const auto menu = ScanPrograms('C');
	for (const auto& e : menu.entries) {
		EXPECT_NE(e.exe, "_INSTALL.BAT");
	}
}

// MOUNT -run runs the one program the skip rules leave, so the scan
// keeps it instead of answering nothing.
TEST_F(ArchiveChooserScanTest, ScanProgramsKeepsTheOneProgramAutoexecLeaves)
{
	File("AUTOEXEC.BAT");
	File("GAME.EXE");
	const auto scan = ScanPrograms('C');
	ASSERT_EQ(scan.entries.size(), 1u);
	EXPECT_EQ(scan.entries[0].exe, "GAME.EXE");
	EXPECT_FALSE(scan.has_dosbox_bat);
}

TEST_F(ArchiveChooserScanTest, ScanProgramsReportsADosboxBat)
{
	File("DOSBOX.BAT");
	File("GAME.EXE");
	File("SETUP.EXE");
	const auto scan = ScanPrograms('C');
	EXPECT_TRUE(scan.has_dosbox_bat);
	EXPECT_EQ(scan.entries.size(), 2u);
}

// A DOS-legal name that Resolve refuses would be offered and start nothing.
TEST_F(ArchiveChooserScanTest, ScanProgramsLeavesOutNamesResolveRefuses)
{
	File("R&D.EXE");
	File("GAME.EXE");
	File("SETUP.EXE");
	const auto scan = ScanPrograms('C');
	for (const auto& e : scan.entries) {
		EXPECT_NE(e.exe, "R&D.EXE");
	}
	EXPECT_EQ(scan.entries.size(), 2u);
}

TEST_F(ArchiveChooserScanTest, SubdirectoryExesAreFound)
{
	File("KEEN/KEEN4E.EXE");
	File("KEEN/SETUP.EXE");
	File("README.EXE");
	const auto menu   = ScanPrograms('C');
	bool found_subdir = false;
	for (const auto& e : menu.entries) {
		if (e.exe.find("KEEN\\") != std::string::npos) {
			found_subdir = true;
		}
	}
	EXPECT_TRUE(found_subdir);
}

TEST_F(ArchiveChooserScanTest, NoExesReturnsNullopt)
{
	File("README.TXT");
	EXPECT_TRUE(ScanPrograms('C').entries.empty());
}

TEST_F(ArchiveChooserScanTest, InvalidDriveLetterReturnsNullopt)
{
	EXPECT_TRUE(ScanPrograms('1').entries.empty());
}

} // namespace
