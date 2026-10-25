// SPDX-FileCopyrightText:  2020-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 dosbox-automation contributors

#include "shell/shell.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "shell/shell_cmds.cpp"
#include "dos/dos.h"
#include "dos/drives.h"
#include "dos/programs/mount.h"
#include "dos/programs/mount_policy.h"
#include "dosbox_test_fixture.h"
#include "test_temp_dir.h"
#include "utils/string_utils.h"

namespace {

using namespace testing;

class DOS_Shell_CMDSTest : public DOSBoxTestFixture {};

class MockDOS_Shell : public DOS_Shell {
public:
	/**
	 * NOTE: If we need to call the actual object, we use this. By
	 * default, the mocked functions return whatever we tell it to
	 * (if given a .WillOnce(Return(...)), or a default value
	 * (false).
	 *
	 * MockDOS_Shell()
	 * {
	 * 	// delegate call to the real object.
	 * 	ON_CALL(*this, ExecuteShellCommand)
	 * 	        .WillByDefault([this](char *name, char *arguments) {
	 * 		        return real_.ExecuteShellCommand(name,
	 * arguments);
	 * 	        });
	 * }
	 */
	MOCK_METHOD(bool, ExecuteShellCommand,
	            (const char* const name, char* arguments), (override));

//	MOCK_METHOD(void, WriteOut, (const char* format, const char* arguments),
//	            (override));

private:
	DOS_Shell real_; // Keeps an instance of the real in the mock.
};

void assert_DoCommand(std::string input, std::string expected_name,
                      std::string expected_args)
{
	MockDOS_Shell shell;
	auto input_c_str = const_cast<char*>(input.c_str());
	EXPECT_CALL(shell,
	            ExecuteShellCommand(StrEq(expected_name), StrEq(expected_args)))
	        .Times(1)
	        .WillOnce(Return(true));
	EXPECT_NO_THROW({ shell.DoCommand(input_c_str); });
}

// Tests chars that separate command name from arguments
TEST_F(DOS_Shell_CMDSTest, DoCommand_Separating_Chars)
{
	// These should all cause the parser to stop
	std::vector<char> end_chars{
	        32,
	        '/',
	        '\t',
	        '=',
	        '"',
	};
	for (auto end_chr : end_chars) {
		MockDOS_Shell shell;
		std::string name  = "PATH";
		std::string args  = "";
		std::string input = name;
		input += end_chr;
		input += "ARG";
		args += end_chr;
		args += "ARG";
		assert_DoCommand(input, name, args);
	}
}

// COMMAND.COM also ends the command word at ';' and ',', so the MS-DOS 5.0
// reference spellings "append;" and "path;" reach their commands.
TEST_F(DOS_Shell_CMDSTest, DoCommand_Semicolon_And_Comma_Separate)
{
	assert_DoCommand("PATH;", "PATH", ";");
	assert_DoCommand("APPEND;", "APPEND", ";");
	assert_DoCommand("DIR,", "DIR", ",");
	assert_DoCommand("ECHO,HI", "ECHO", ",HI");
}

TEST_F(DOS_Shell_CMDSTest, DoCommand_All_Cmds_Do_Valid_Execute)
{
	MockDOS_Shell shell;
	for (std::pair<std::string, SHELL_Cmd> cmd : shell_cmds) {
		std::string input = cmd.first;
		assert_DoCommand(input, input, "");
	}
}

TEST_F(DOS_Shell_CMDSTest, DoCommand_Trim_Space)
{
	assert_DoCommand(" PATH ", "PATH", "");
}

TEST_F(DOS_Shell_CMDSTest, DoCommand_Splits_Cmd_and_Args)
{
	// NOTE: It does not strip the arguments!
	assert_DoCommand("DIR *.*", "DIR", " *.*");
}

TEST_F(DOS_Shell_CMDSTest, DoCommand_Doesnt_Split_Colon)
{
	// ensure we don't split on colon ...
	assert_DoCommand("C:", "C:", "");
	// ... but it does split on slash
	assert_DoCommand("C:\\", "C:", "\\");
}

TEST_F(DOS_Shell_CMDSTest, DoCommand_Nospace_Dot_Handling)
{
	assert_DoCommand("DIR.EXE", "DIR", ".EXE");
	assert_DoCommand("CD..", "CD", "..");
	assert_DoCommand("CD....", "CD", "....");
}

TEST_F(DOS_Shell_CMDSTest, DoCommand_Nospace_Slash_Handling)
{
	assert_DoCommand("CD\\DIRECTORY", "CD", "\\DIRECTORY");
	assert_DoCommand("CD\\", "CD", "\\");
}

TEST_F(DOS_Shell_CMDSTest, DoCommand_Nospace_Echo_DoubleQuotes)
{
	assert_DoCommand("ECHO\"", "ECHO", "\"");
	assert_DoCommand("ECHO\"\"", "ECHO", "\"\"");
}

TEST_F(DOS_Shell_CMDSTest, DoCommand_Nospace_If_DoubleQuotes)
{
	assert_DoCommand("IF\"1\"==\"1\"", "IF", "\"1\"==\"1\"");
}

TEST_F(DOS_Shell_CMDSTest, CMD_ECHO_off_on)
{
	MockDOS_Shell shell;
	EXPECT_TRUE(shell.echo); // should be the default
//	EXPECT_CALL(shell, WriteOut(_, _)).Times(0);
	EXPECT_NO_THROW({ shell.CMD_ECHO(const_cast<char*>("OFF")); });
	EXPECT_FALSE(shell.echo);
	EXPECT_NO_THROW({ shell.CMD_ECHO(const_cast<char*>("ON")); });
	EXPECT_TRUE(shell.echo);
}

TEST_F(DOS_Shell_CMDSTest, CMD_ECHO_space_handling)
{
	MockDOS_Shell shell;

	EXPECT_TRUE(shell.echo);
//	EXPECT_CALL(shell, WriteOut(_, StrEq("OFF "))).Times(1);
	// this DOES NOT trigger ECHO OFF (trailing space causes it to not)
	EXPECT_NO_THROW({ shell.CMD_ECHO(const_cast<char*>(" OFF ")); });
	EXPECT_TRUE(shell.echo);

//	EXPECT_CALL(shell, WriteOut(_, StrEq("FF "))).Times(1);
	// this DOES NOT trigger ECHO OFF (initial 'O' gets stripped)
	EXPECT_NO_THROW({ shell.CMD_ECHO(const_cast<char*>("OFF ")); });
	EXPECT_TRUE(shell.echo);

	// no trailing space, echo off should work
//	EXPECT_CALL(shell, WriteOut(_, _)).Times(0);
	EXPECT_NO_THROW({ shell.CMD_ECHO(const_cast<char*>(" OFF")); });
	// check that OFF worked properly, despite spaces
	EXPECT_FALSE(shell.echo);

	// NOTE: the expected string here is missing the leading char of the
	// input to ECHO. the first char is stripped as it's assumed it will be
	// a space, period or slash.
//	EXPECT_CALL(shell, WriteOut(_, StrEq("    HI "))).Times(1);
	EXPECT_NO_THROW({ shell.CMD_ECHO(const_cast<char*>(".    HI ")); });
}

TEST_F(DOS_Shell_CMDSTest, CMD_FOR_basic)
{
	MockDOS_Shell shell = {};

	EXPECT_CALL(shell, ExecuteShellCommand(StrEq("ECHO"), StrEq(" ONE")))
	        .Times(1)
	        .WillOnce(Return(true));
	EXPECT_CALL(shell, ExecuteShellCommand(StrEq("ECHO"), StrEq(" TWO")))
	        .Times(1)
	        .WillOnce(Return(true));
	EXPECT_NO_THROW({
		shell.CMD_FOR(const_cast<char*>(" %C IN (ONE TWO) DO ECHO %C"));
	});
}

TEST_F(DOS_Shell_CMDSTest, CMD_FOR_delimiters)
{
	MockDOS_Shell shell = {};

	static constexpr auto delimeters = ",;= \t";
	// TODO: C++20: Use std::format();
	const auto input = format_str("%s%%C%sIN%s(%sONE%sTWO%s)%sDO%sECHO %%C",
	                              delimeters,
	                              delimeters,
	                              delimeters,
	                              delimeters,
	                              delimeters,
	                              delimeters,
	                              delimeters,
	                              delimeters); // ):

	EXPECT_CALL(shell, ExecuteShellCommand(StrEq("ECHO"), StrEq(" ONE")))
	        .Times(1)
	        .WillOnce(Return(true));
	EXPECT_CALL(shell, ExecuteShellCommand(StrEq("ECHO"), StrEq(" TWO")))
	        .Times(1)
	        .WillOnce(Return(true));
	EXPECT_NO_THROW({ shell.CMD_FOR(const_cast<char*>(input.c_str())); });
}

TEST_F(DOS_Shell_CMDSTest, CMD_FOR_missing_do)
{
	MockDOS_Shell shell = {};

	EXPECT_CALL(shell, ExecuteShellCommand(_, _)).Times(0);
	EXPECT_NO_THROW({
		shell.CMD_FOR(const_cast<char*>(" %C IN (ONE TWO) ECHO %C"));
	});
}

TEST_F(DOS_Shell_CMDSTest, CMD_FOR_missing_in)
{
	MockDOS_Shell shell = {};

	EXPECT_CALL(shell, ExecuteShellCommand(_, _)).Times(0);
	EXPECT_NO_THROW({
		shell.CMD_FOR(const_cast<char*>(" %C (ONE TWO) DO ECHO %C"));
	});
}

TEST_F(DOS_Shell_CMDSTest, CMD_FOR_missing_var)
{
	MockDOS_Shell shell = {};

	EXPECT_CALL(shell, ExecuteShellCommand(_, _)).Times(0);
	EXPECT_NO_THROW({
		shell.CMD_FOR(const_cast<char*>(" IN (ONE TWO) DO ECHO %C"));
	});
}

TEST_F(DOS_Shell_CMDSTest, CMD_FOR_missing_parens)
{
	MockDOS_Shell shell = {};

	EXPECT_CALL(shell, ExecuteShellCommand(_, _)).Times(0);
	EXPECT_NO_THROW({
		shell.CMD_FOR(const_cast<char*>(" %C IN ONE TWO DO ECHO %C"));
	});
}

TEST_F(DOS_Shell_CMDSTest, CMD_FOR_missing_command)
{
	MockDOS_Shell shell = {};

	EXPECT_CALL(shell, ExecuteShellCommand(_, _)).Times(0);
	EXPECT_NO_THROW(
	        { shell.CMD_FOR(const_cast<char*>(" %C IN (ONE TWO) DO")); });
}

TEST_F(DOS_Shell_CMDSTest, CMD_FOR_for_not_allowed)
{
	MockDOS_Shell shell = {};

	EXPECT_CALL(shell, ExecuteShellCommand(_, _)).Times(0);
	EXPECT_NO_THROW({
		shell.CMD_FOR(const_cast<char*>(
		        " %C IN (ONE TWO) DO FOR %D IN (THREE FOUR) DO ECHO %D"));
	});
}

// Every dispatched command as name plus arguments; a line listed in
// queues_on queues its lines the way MOUNT -run does, a name in delegated
// goes to the real shell.
class PendingCommandsTest : public DOSBoxTestFixture {
protected:
	void SetUp() override
	{
		DOSBoxTestFixture::SetUp();
		PrepareDos();
		// Program's constructor reads the PSP, so DOS must be up first.
		shell_owner = std::make_unique<NiceMock<MockDOS_Shell>>();
		ON_CALL(shell(), ExecuteShellCommand(_, _))
		        .WillByDefault([this](const char* const name, char* args) {
			        const auto line = std::string(name) + args;
			        seen.push_back(line);
			        if (const auto it = queues_on.find(line);
			            it != queues_on.end()) {
				        EXPECT_TRUE(SHELL_QueuePendingCommands(
				                it->second));
			        }
			        if (delegated.contains(name)) {
				        return shell().DOS_Shell::ExecuteShellCommand(
				                name, args);
			        }
			        return true;
		        });
	}
	virtual void PrepareDos() {}
	void TearDown() override
	{
		SHELL_TakePendingCommands();
		shell_owner.reset();
		RestoreDosState();
		DOSBoxTestFixture::TearDown();
	}
	virtual void RestoreDosState() {}
	void Parse(const std::string& line)
	{
		char buffer[CMD_MAXLINE] = {};
		safe_strcpy(buffer, line.c_str());
		shell().ParseLine(buffer);
	}
	NiceMock<MockDOS_Shell>& shell()
	{
		return *shell_owner;
	}

	std::unique_ptr<NiceMock<MockDOS_Shell>> shell_owner      = {};
	std::vector<std::string> seen                             = {};
	std::map<std::string, std::vector<std::string>> queues_on = {};
	std::set<std::string> delegated                           = {};
};

// A PSP with a file table and C: on a temp folder, so CALL reads a real
// .BAT; the base fixture has neither.
class PendingCommandsCallTest : public PendingCommandsTest {
protected:
	void PrepareDos() override
	{
		tmp = TestTempDir::MakeUnique("pending_call_");
		ASSERT_FALSE(tmp.empty());
		previous_drive    = Drives.at(DriveC);
		previous_default  = DOS_GetDefaultDrive();
		Drives.at(DriveC) = std::make_shared<localDrive>(
		        (tmp.string() + CROSS_FILESPLIT).c_str(),
		        512,
		        32,
		        32765,
		        16000,
		        0xf8,
		        false,
		        true);
		DOS_SetDefaultDrive(DriveC);

		constexpr uint16_t PspParagraphs = 0x10;
		previous_psp                     = dos.psp();
		dos.psp(MCB_DOS);
		uint16_t blocks = PspParagraphs;
		ASSERT_TRUE(DOS_AllocateMemory(&psp_segment, &blocks));
		DOS_MCB(psp_segment - 1).SetPSPSeg(psp_segment);
		DOS_PSP psp(psp_segment);
		psp.MakeNew(PspParagraphs);
		dos.psp(psp_segment);
		blocks = env_paragraphs;
		ASSERT_TRUE(DOS_AllocateMemory(&env_segment, &blocks));
		for (int i = 0; i < env_paragraphs * 16; ++i) {
			mem_writeb(PhysicalMake(env_segment, i), 0);
		}
		psp.SetEnvironment(env_segment);
	}
	void RestoreDosState() override
	{
		dos.psp(previous_psp);
		if (env_segment != 0) {
			DOS_FreeMemory(env_segment);
		}
		if (psp_segment != 0) {
			DOS_FreeMemory(psp_segment);
		}
		DOS_SetDefaultDrive(previous_default);
		Drives.at(DriveC)  = previous_drive;
		std::error_code ec = {};
		std_fs::remove_all(tmp, ec);
	}
	void WriteHostFile(const std::string& name, const std::string& text)
	{
		std::ofstream(tmp / name, std::ios::binary) << text;
	}

	static constexpr uint8_t DriveC           = 2;
	std_fs::path tmp                          = {};
	std::shared_ptr<DOS_Drive> previous_drive = {};
	uint8_t previous_default                  = 0;
	uint16_t previous_psp                     = 0;
	uint16_t psp_segment                      = 0;
	uint16_t env_segment                      = 0;
	uint16_t env_paragraphs                   = 0x10;
};

TEST_F(PendingCommandsTest, QueuedCommandsRunAfterTheLineThatQueuedThem)
{
	queues_on["QUEUER"] = {"C:", "GAME.EXE"};
	Parse("QUEUER");
	EXPECT_THAT(seen, ElementsAre("QUEUER"));
	shell().RunBatchFile();
	EXPECT_THAT(seen, ElementsAre("QUEUER", "C:", "GAME.EXE"));
}

TEST_F(PendingCommandsTest, ASecondQueueWhileOnePendsIsRefused)
{
	EXPECT_TRUE(SHELL_QueuePendingCommands({"FIRST"}));
	EXPECT_FALSE(SHELL_QueuePendingCommands({"SECOND"}));
	EXPECT_THAT(SHELL_TakePendingCommands(), ElementsAre("FIRST"));
}

TEST_F(PendingCommandsTest, AnEmptyQueueIsRefused)
{
	EXPECT_FALSE(SHELL_QueuePendingCommands({}));
	EXPECT_TRUE(SHELL_TakePendingCommands().empty());
}

TEST_F(PendingCommandsTest, TheQueueIsConsumedOnce)
{
	queues_on["QUEUER"] = {"GAME.EXE"};
	Parse("QUEUER");
	shell().RunBatchFile();
	seen.clear();
	Parse("NEXT");
	shell().RunBatchFile();
	EXPECT_THAT(seen, ElementsAre("NEXT"));
}

// A queued line that pushes a batch (CALL GAME.BAT) must see that batch
// finish before the next queued line (EXIT) runs.
TEST_F(PendingCommandsTest, LinesQueuedByAQueuedLineRunBeforeTheNextOne)
{
	queues_on["QUEUER"] = {"FIRST", "LAST"};
	queues_on["FIRST"]  = {"INNER"};
	Parse("QUEUER");
	shell().RunBatchFile();
	EXPECT_THAT(seen, ElementsAre("QUEUER", "FIRST", "INNER", "LAST"));
}

TEST_F(PendingCommandsTest, QueuedLinesAreNotExpandedAsBatchLines)
{
	queues_on["QUEUER"] = {"RUN %PATH% %0 %1 100%"};
	Parse("QUEUER");
	shell().RunBatchFile();
	EXPECT_THAT(seen, ElementsAre("QUEUER", "RUN %PATH% %0 %1 100%"));
}

TEST_F(PendingCommandsTest, ControlCharactersAndOverlongLinesAreRefused)
{
	EXPECT_FALSE(SHELL_QueuePendingCommands({"GAME.EXE\r\nDEL *.*"}));
	EXPECT_FALSE(SHELL_QueuePendingCommands({std::string("A\0B", 3)}));
	EXPECT_FALSE(SHELL_QueuePendingCommands({"OK", "BAD\x1b"}));
	EXPECT_FALSE(SHELL_QueuePendingCommands({"OK", ""}));
	EXPECT_FALSE(SHELL_QueuePendingCommands({std::string(CMD_MAXLINE - 1, 'A')}));
	EXPECT_TRUE(SHELL_TakePendingCommands().empty());
}

// The batch reader hands "@" plus the line to a CMD_MAXLINE buffer.
TEST_F(PendingCommandsTest, TheLongestLineThatFitsIsKept)
{
	const auto longest  = std::string(CMD_MAXLINE - 2, 'A');
	queues_on["QUEUER"] = {longest};
	Parse("QUEUER");
	shell().RunBatchFile();
	EXPECT_THAT(seen, ElementsAre("QUEUER", longest));
}

TEST_F(PendingCommandsCallTest, ACalledBatchFinishesBeforeTheNextQueuedLine)
{
	WriteHostFile("GAME.BAT", "INSIDE\r\n");
	delegated           = {"CALL", "GAME", "GAME.BAT", "EXIT"};
	queues_on["QUEUER"] = {"CALL GAME.BAT", "EXIT"};
	Parse("QUEUER");
	shell().RunBatchFile();
	std::vector<std::string> order = {};
	std::copy_if(seen.begin(),
	             seen.end(),
	             std::back_inserter(order),
	             [](const auto& line) {
		             return line == "INSIDE" || line == "EXIT";
	             });
	EXPECT_THAT(order, ElementsAre("INSIDE", "EXIT"))
	        << ::testing::PrintToString(seen);
}

// ada-cw1y: text after an expansion that filled SET's line buffer was
// written past its end.
class SetCommandTest : public PendingCommandsCallTest {
protected:
	SetCommandTest()
	{
		env_paragraphs = 0x200;
	}
};

TEST_F(SetCommandTest, AnExpansionPastTheLineLengthIsCutThere)
{
	DOS_PSP psp(psp_segment);
	ASSERT_TRUE(psp.SetEnvironmentValue("BIG", std::string(3000, 'A')));

	char args[] = "X=%BIG%%BIG%tail";
	shell().CMD_SET(args);

	const auto value = psp.GetEnvironmentValue("X");
	ASSERT_TRUE(value.has_value());
	EXPECT_EQ(*value, std::string(CMD_MAXLINE - 1, 'A'));
}

// ada-rbal: only the last path element says whether an extension was
// typed (COMMAND.COM's Strip, MS-DOS 4.0 v4.0/src/CMD/COMMAND/PATH2.ASM).
class ProgramLookupTest : public PendingCommandsCallTest {
protected:
	void EnterSubdirectory(const std_fs::path& host_relative,
	                       const char* const dos_path)
	{
		WriteHostFile("AA.BAT", "@ECHO OFF\r\n");
		std_fs::create_directories(tmp / host_relative);
		Drives.at(DriveC)->EmptyCache();
		ASSERT_TRUE(DOS_ChangeDir(dos_path));
	}
};

TEST_F(ProgramLookupTest, ARelativeNameWithoutExtensionFindsTheBatch)
{
	EnterSubdirectory("TEST", "\\TEST");
	EXPECT_TRUE(shell().ExecuteProgram("..\\AA", ""));
}

TEST_F(ProgramLookupTest, TwoLevelsUpWithoutExtensionFindsTheBatch)
{
	EnterSubdirectory(std_fs::path("ONE") / "TWO", "\\ONE\\TWO");
	EXPECT_TRUE(shell().ExecuteProgram("..\\..\\AA", ""));
}

TEST_F(ProgramLookupTest, ADotInADirectoryNameIsNoExtension)
{
	std_fs::create_directories(tmp / "V1.0");
	std::ofstream(tmp / "V1.0" / "GAME.BAT", std::ios::binary) << "@ECHO OFF\r\n";
	Drives.at(DriveC)->EmptyCache();
	EXPECT_TRUE(shell().ExecuteProgram("V1.0\\GAME", ""));
}

// ada-jyw6: the host path SUBST builds from a local drive's basedir is
// host text, not code page text for MOUNT to convert.
class SubstTest : public DOSBoxTestFixture {
protected:
	void SetUp() override
	{
		DOSBoxTestFixture::SetUp();
		init_config_dir();
		MountPolicy::InitPolicyConfig({});
		tmp = TestTempDir::MakeUnique("subst_test_");
		ASSERT_FALSE(tmp.empty());
	}
	void TearDown() override
	{
		Drives.at(drive_index(BaseLetter))  = nullptr;
		Drives.at(drive_index(SubstLetter)) = nullptr;
		std::error_code ec = {};
		std_fs::remove_all(tmp, ec);
		DOSBoxTestFixture::TearDown();
	}

	bool MountBase(const std::string& typed_path)
	{
		const auto line = std::string(1, BaseLetter) + " \"" + typed_path + "\"";
		auto mount = std::make_unique<MOUNT>();
		return mount->ProcessArguments(new CommandLine("Z:\\MOUNT.COM", line))
		        .has_value();
	}

	// Where X: points after the SUBST, or nowhere.
	std_fs::path Subst(const std::string& dos_dir)
	{
		auto args = std::string(1, SubstLetter) + ": " + dos_dir;
		DOS_Shell shell;
		shell.CMD_SUBST(args.data());
		const auto local = std::dynamic_pointer_cast<localDrive>(
		        Drives.at(drive_index(SubstLetter)));
		return local ? std_fs::path(local->GetBasedir()) : std_fs::path();
	}
	static bool Same(const std_fs::path& a, const std_fs::path& b)
	{
		std::error_code ec = {};
		return !a.empty() && std_fs::equivalent(a, b, ec);
	}

	static constexpr char BaseLetter  = 'L';
	static constexpr char SubstLetter = 'X';
	std_fs::path tmp                  = {};
};

TEST_F(SubstTest, AnAsciiBasedirIsSubstituted)
{
	std_fs::create_directories(tmp / "plain" / "GAMES");
	ASSERT_TRUE(MountBase((tmp / "plain").string()));
	EXPECT_TRUE(Same(Subst("L:\\GAMES"), tmp / "plain" / "GAMES"));
}

TEST_F(SubstTest, ACodePageNameOnAnAsciiBasedirIsSubstituted)
{
	// A-umlaut typed as 0x8E names the UTF-8 folder, as MOUNT reads it.
	const auto folder = tmp / "plain" / std_fs::path(u8"\u00C4RGER");
	std_fs::create_directories(folder);
	ASSERT_TRUE(MountBase((tmp / "plain").string()));
	EXPECT_TRUE(Same(Subst("L:\\\x8ERGER"), folder));
}

TEST_F(SubstTest, ANonAsciiBasedirIsSubstituted)
{
	std_fs::create_directories(tmp / std_fs::path(u8"f\u00FCr") / "GAMES");
	// u-umlaut is 0x81 in code page 437.
	ASSERT_TRUE(MountBase((tmp / "f").string() + "\x81r"));
	EXPECT_TRUE(Same(Subst("L:\\GAMES"),
	                 tmp / std_fs::path(u8"f\u00FCr") / "GAMES"));
}

TEST_F(SubstTest, SecureModeRefusesTheSubstitution)
{
	std_fs::create_directories(tmp / "plain" / "GAMES");
	ASSERT_TRUE(MountBase((tmp / "plain").string()));
	control->SwitchToSecureMode();
	EXPECT_TRUE(Subst("L:\\GAMES").empty());
}

} // namespace
