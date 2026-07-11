// SPDX-FileCopyrightText:  2020-2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

/* This sample shows how to write a simple unit test for dosbox-automation using
 * Google C++ testing framework.
 *
 * Read Google Test Primer for reference of most available features, macros,
 * and guidance about writing unit tests:
 *
 * https://github.com/google/googletest/blob/master/googletest/docs/primer.md#googletest-primer
 */

/* Include necessary header files; order of headers should be as follows:
 *
 * 1. Header declaring functions/classes being tested
 * 2. <gtest/gtest.h>, which declares the testing framework
 * 3. Additional system headers (if needed)
 * 4. Additional dosbox-automation headers (if needed)
 */

#include "dos/dos.h"

#include <iterator>
#include <string>

#include <gtest/gtest.h>

#include "config/config.h"
#include "dos/dos_keyboard_layout.h"
#include "dos/dos_system.h"
#include "dos/drives.h"
#include "shell/shell.h"
#include "utils/string_utils.h"

#include "dosbox_test_fixture.h"
#include "dos/dos_files.cpp"

namespace {

class DOS_FilesTest : public DOSBoxTestFixture {};

void assert_DTAExtendName(const std::string& input_fullname,
                          const std::string_view expected_name,
                          const std::string_view expected_ext)
{
	const auto [output_name, output_ext] = DTAExtendName(input_fullname.c_str());

	// mutates input up to dot
	EXPECT_EQ(output_name, expected_name);
	EXPECT_EQ(output_ext, expected_ext);
}

void assert_DOS_MakeName(const char* const input, bool exp_result,
                         std::string exp_fullname = "", int exp_drive = 0)
{
	uint8_t drive_result;
	char fullname_result[DOS_PATHLENGTH];
	bool result = DOS_MakeName(input, fullname_result, &drive_result);
	EXPECT_EQ(result, exp_result);
	// if we expected success, also test these
	if (exp_result) {
		EXPECT_EQ(std::string(fullname_result), exp_fullname);
		EXPECT_EQ(drive_result, exp_drive);
	}
}

TEST_F(DOS_FilesTest, DOS_MakeName_Basic_Failures)
{
	// make sure we get failures, not explosions
	assert_DOS_MakeName("\0", false);
	assert_DOS_MakeName(" ", false);
	assert_DOS_MakeName(" NAME", false);
	assert_DOS_MakeName("\1:\\AUTOEXEC.BAT", false);
	assert_DOS_MakeName(nullptr, false);
	assert_DOS_MakeName("B:\\AUTOEXEC.BAT", false);
}

TEST_F(DOS_FilesTest, DOS_MakeName_Z_AUTOEXEC_BAT_exists)
{
	assert_DOS_MakeName("Z:\\AUTOEXEC.BAT", true, "AUTOEXEC.BAT", 25);
}

// This captures a particularity of the DOSBox code where the
// drive index is set even though the path failed. this could have
// ramifications across the codebase if not replicated
TEST_F(DOS_FilesTest, DOS_MakeName_Drive_Index_Set_On_Failure)
{
	uint8_t drive_result;
	char fullname_result[DOS_PATHLENGTH];
	bool result;
	result = DOS_MakeName("A:\r\n", fullname_result, &drive_result);
	EXPECT_EQ(result, false);
	EXPECT_EQ(drive_result, 0);
	result = DOS_MakeName("B:\r\n", fullname_result, &drive_result);
	EXPECT_EQ(drive_result, 1);
	EXPECT_EQ(result, false);
	result = DOS_MakeName("C:\r\n", fullname_result, &drive_result);
	EXPECT_EQ(drive_result, 2);
	EXPECT_EQ(result, false);
	result = DOS_MakeName("Z:\r\n", fullname_result, &drive_result);
	EXPECT_EQ(drive_result, 25);
	EXPECT_EQ(result, false);
}

TEST_F(DOS_FilesTest, DOS_MakeName_Uppercase)
{
	assert_DOS_MakeName("Z:\\autoexec.bat", true, "AUTOEXEC.BAT", 25);
	// lower case
	assert_DOS_MakeName("z:\\AUTOEXEC.BAT", true, "AUTOEXEC.BAT", 25);
	// current dir isn't uppercased if it's not already
	safe_strcpy(Drives.at(25)->curdir, "Windows\\Folder");
	assert_DOS_MakeName("autoexec.bat", true,
	                    "Windows\\Folder\\AUTOEXEC.BAT", 25);
}

TEST_F(DOS_FilesTest, DOS_MakeName_CONVERTS_FWD_SLASH)
{
	assert_DOS_MakeName("Z:/AUTOEXEC.BAT", true, "AUTOEXEC.BAT", 25);
}

// spaces get stripped out before processing (\t, \r, etc, are illegal chars,
// not whitespace)
TEST_F(DOS_FilesTest, DOS_MakeName_STRIP_SPACE)
{
	assert_DOS_MakeName("Z:\\   A U T  OE X   EC     .BAT", true,
	                    "AUTOEXEC.BAT", 25);
	assert_DOS_MakeName("Z: \\   A U T  OE X   EC     .BAT", true,
	                    "AUTOEXEC.BAT", 25);
	assert_DOS_MakeName("12345   678.123", true, "12345678.123", 25);
	// except here, whitespace isn't stripped & causes failure
	assert_DOS_MakeName("Z :\\AUTOEXEC.BAT", false);
}

TEST_F(DOS_FilesTest, DOS_MakeName_Dir_Handling)
{
	assert_DOS_MakeName("Z:\\CODE\\", true, "CODE", 25);
	assert_DOS_MakeName("Z:\\CODE\\AUTOEXEC.BAT", true, "CODE\\AUTOEXEC.BAT", 25);
	assert_DOS_MakeName("Z:\\DIR\\UNTERM", true, "DIR\\UNTERM", 25);
	// trailing gets trimmed
	assert_DOS_MakeName("Z:\\CODE\\TERM\\", true, "CODE\\TERM", 25);
}

TEST_F(DOS_FilesTest, DOS_MakeName_Assumes_Current_Drive_And_Dir)
{
	// when passed only a filename, assume default drive and current dir
	assert_DOS_MakeName("AUTOEXEC.BAT", true, "AUTOEXEC.BAT", 25);
	// artificially change directory
	safe_strcpy(Drives.at(25)->curdir, "CODE");
	assert_DOS_MakeName("AUTOEXEC.BAT", true, "CODE\\AUTOEXEC.BAT", 25);
	// artificially change directory
	safe_strcpy(Drives.at(25)->curdir, "CODE\\BIN");
	assert_DOS_MakeName("AUTOEXEC.BAT", true, "CODE\\BIN\\AUTOEXEC.BAT", 25);
	// ignores current dir and goes to root
	assert_DOS_MakeName("\\AUTOEXEC.BAT", true, "AUTOEXEC.BAT", 25);
	safe_strcpy(Drives.at(25)->curdir, "");
	assert_DOS_MakeName("Z:\\CODE\\BIN", true, "CODE\\BIN", 25);
	assert_DOS_MakeName("Z:", true, "", 25);
	assert_DOS_MakeName("Z:\\", true, "", 25);
	// This is a bug but we need to capture this functionality
	safe_strcpy(Drives.at(25)->curdir, "CODE\\BIN\\");
	assert_DOS_MakeName("AUTOEXEC.BAT", true, "CODE\\BIN\\\\AUTOEXEC.BAT", 25);
	safe_strcpy(Drives.at(25)->curdir, "CODE\\BIN\\\\");
	assert_DOS_MakeName("AUTOEXEC.BAT", true, "CODE\\BIN\\\\\\AUTOEXEC.BAT", 25);
}

// This tests that illegal char matching happens AFTER 8.3 trimming
TEST_F(DOS_FilesTest, DOS_MakeName_Illegal_Chars_After_8_3)
{
	safe_strcpy(Drives.at(25)->curdir, "BIN");
	assert_DOS_MakeName("\n2345678AAAAABBB.BAT", false);
	assert_DOS_MakeName("12345678.\n23BBBBBAAA", false);
	assert_DOS_MakeName("12345678AAAAABB\n.BAT", true, "BIN\\12345678.BAT", 25);
	assert_DOS_MakeName("12345678.123BBBBBAAA\n", true, "BIN\\12345678.123", 25);
}

TEST_F(DOS_FilesTest, DOS_MakeName_DOS_PATHLENGTH_checks)
{
	// Right on the line ...
	safe_strcpy(Drives.at(25)->curdir,
	            "aaaaaaaaa\\aaaaaaaaa\\aaaaaaaaa\\"
	            "aaaaaaaaa\\aaaaaaaaa\\aaaaaaaaa\\aaaaaaaaaa");
	assert_DOS_MakeName("BBBBB.BB", true,
	                    "aaaaaaaaa\\aaaaaaaaa\\aaaaaaaaa\\aaaaaaaaa\\"
	                    "aaaaaaaaa\\aaaaaaaaa\\aaaaaaaaaa\\BBBBB.BB",
	                    25);
	// Equal to...
	assert_DOS_MakeName("BBBBBB.BB", false);
	// Over...
	assert_DOS_MakeName("BBBBBBB.BB", false);
}

TEST_F(DOS_FilesTest, DOS_MakeName_Enforce_8_3)
{
	safe_strcpy(Drives.at(25)->curdir, "BIN");
	assert_DOS_MakeName("12345678AAAAABBBB.BAT", true, "BIN\\12345678.BAT", 25);
	assert_DOS_MakeName("12345678.123BBBBBAAAA", true, "BIN\\12345678.123", 25);
}

TEST_F(DOS_FilesTest, DOS_MakeName_Dot_Handling)
{
	safe_strcpy(Drives.at(25)->curdir, "WINDOWS\\CONFIG");
	assert_DOS_MakeName(".", true, "WINDOWS\\CONFIG", 25);
	assert_DOS_MakeName("..", true, "WINDOWS", 25);
	assert_DOS_MakeName("...", true, "", 25);
	assert_DOS_MakeName(".\\AUTOEXEC.BAT", true,
	                    "WINDOWS\\CONFIG\\AUTOEXEC.BAT", 25);
	assert_DOS_MakeName("..\\AUTOEXEC.BAT", true, "WINDOWS\\AUTOEXEC.BAT", 25);
	assert_DOS_MakeName("...\\AUTOEXEC.BAT", true, "AUTOEXEC.BAT", 25);
	safe_strcpy(Drives.at(25)->curdir, "WINDOWS\\CONFIG\\FOLDER");
	assert_DOS_MakeName("...\\AUTOEXEC.BAT", true, "WINDOWS\\AUTOEXEC.BAT", 25);
	assert_DOS_MakeName("....\\AUTOEXEC.BAT", true, "AUTOEXEC.BAT", 25);
	safe_strcpy(Drives.at(25)->curdir, "WINDOWS\\CONFIG\\FOLDER\\DEEP");
	assert_DOS_MakeName("....\\AUTOEXEC.BAT", true, "WINDOWS\\AUTOEXEC.BAT", 25);
	assert_DOS_MakeName(".....\\AUTOEXEC.BAT", true, "AUTOEXEC.BAT", 25);
	// make sure we can exceed the depth
	assert_DOS_MakeName("......\\AUTOEXEC.BAT", true, "AUTOEXEC.BAT", 25);
	assert_DOS_MakeName("...........\\AUTOEXEC.BAT", true, "AUTOEXEC.BAT", 25);
	// make sure we have arbitrary expansion
	assert_DOS_MakeName("...\\FOLDER\\...\\AUTOEXEC.BAT", true,
	                    "WINDOWS\\AUTOEXEC.BAT", 25);
	assert_DOS_MakeName("...\\FOLDER\\....\\.\\AUTOEXEC.BAT", true,
	                    "AUTOEXEC.BAT", 25);
}

TEST_F(DOS_FilesTest, DOS_MakeName_No_SlashSlash)
{
	assert_DOS_MakeName("Z:..\\tmp.txt", true, "TMP.TXT", 25);
}

// Exhaustive test of all good chars
TEST_F(DOS_FilesTest, DOS_MakeName_GoodChars)
{
	unsigned char start_letter = 'A';
	unsigned char start_number = '0';
	std::vector<unsigned char> symbols{'$', '#',  '@',  '(',  ')', '!', '%',
	                                   '{', '}',  '`',  '~',  '_', '-', '.',
	                                   '*', '?',  '&',  '\'', '+', '^', 246,
	                                   255, 0xa0, 0xe5, 0xbd, 0x9d};
	for (unsigned char li = 0; li < 26; li++) {
		for (unsigned char ni = 0; ni < 10; ni++) {
			for (auto &c : symbols) {
				unsigned char input_array[3] = {
				        static_cast<unsigned char>(start_letter + li),
				        static_cast<unsigned char>(start_number + ni),
				        c,
				};
				std::string test_input(reinterpret_cast<char *>(
				                               input_array),
				                       3);
				assert_DOS_MakeName(test_input.c_str(), true,
				                    test_input, 25);
			}
		}
	}
}

TEST_F(DOS_FilesTest, DOS_MakeName_Colon_Illegal_Paths)
{
	assert_DOS_MakeName(":..\\tmp.txt", false);
	assert_DOS_MakeName(" :..\\tmp.txt", false);
	assert_DOS_MakeName(": \\tmp.txt", false);
	assert_DOS_MakeName(":", false);
}

// ensures a fix for dark forces installer
TEST_F(DOS_FilesTest, DOS_FindFirst_Ending_Slash)
{
	// `dos` comes from dos.h
	dos.errorcode = DOSERR_NONE;
	EXPECT_FALSE(DOS_FindFirst("Z:\\DARK\\LFD\\", FatAttributeFlags::Volume, false));
	EXPECT_EQ(dos.errorcode, DOSERR_NO_MORE_FILES);

	dos.errorcode = DOSERR_NONE;
	EXPECT_FALSE(DOS_FindFirst("Z:\\DARK\\", FatAttributeFlags::Volume, false));
	EXPECT_EQ(dos.errorcode, DOSERR_NO_MORE_FILES);

	// volume names alone don't trigger the failure
	dos.errorcode = DOSERR_NONE;
	EXPECT_TRUE(DOS_FindFirst("Z:\\", FatAttributeFlags::Volume, false));
	EXPECT_NE(dos.errorcode, DOSERR_NO_MORE_FILES);

	// volume attr NOT required
	dos.errorcode = DOSERR_NONE;
	EXPECT_FALSE(DOS_FindFirst("Z:\\NOMATCH\\", 0, false));
	EXPECT_EQ(dos.errorcode, DOSERR_NO_MORE_FILES);
}

TEST_F(DOS_FilesTest, DOS_FindFirst_Rejects_Invalid_Names)
{
	// triggers failures via DOS_FindFirst
	EXPECT_FALSE(DOS_FindFirst("Z:\\BAD\nDIR\\HI.TXT", 0, false));
	EXPECT_EQ(dos.errorcode, DOSERR_PATH_NOT_FOUND);
}

TEST_F(DOS_FilesTest, DOS_FindFirst_FindVolume)
{
	dos.errorcode = DOSERR_NONE;
	EXPECT_TRUE(DOS_FindFirst("Z", FatAttributeFlags::Volume, false));
	EXPECT_EQ(dos.errorcode, DOSERR_NONE);
}

TEST_F(DOS_FilesTest, DOS_FindFirst_FindDevice)
{
	dos.errorcode = DOSERR_NONE;
	EXPECT_TRUE(DOS_FindFirst("COM1", FatAttributeFlags::Device, false));
	EXPECT_EQ(dos.errorcode, DOSERR_NONE);
}

// \DEV\ is a virtual directory into the device namespace (DOS 2.0+);
// it must resolve devices without existing on any drive
TEST_F(DOS_FilesTest, DOS_FindDevice_Dev_Prefix)
{
	EXPECT_NE(DOS_FindDevice("\\DEV\\NUL"), DOS_DEVICES);
	EXPECT_NE(DOS_FindDevice("\\DEV\\CON"), DOS_DEVICES);
	EXPECT_NE(DOS_FindDevice("Z:\\DEV\\NUL"), DOS_DEVICES);
	// same device with and without the prefix
	EXPECT_EQ(DOS_FindDevice("\\DEV\\NUL"), DOS_FindDevice("NUL"));
	// extension is ignored on device names, also with the prefix
	EXPECT_NE(DOS_FindDevice("\\DEV\\NUL.TXT"), DOS_DEVICES);
}

// CLOCK$ device: 6-byte record is days since 1980-01-01 (uint16 LE),
// minutes, hours, hundredths, seconds
TEST_F(DOS_FilesTest, DOS_Clock_Device_Read)
{
	EXPECT_NE(DOS_FindDevice("CLOCK$"), DOS_DEVICES);
	EXPECT_NE(DOS_FindDevice("\\DEV\\CLOCK$"), DOS_DEVICES);

	// The fixture has no functional PSP file table, so open in FCB
	// mode where the entry is the real file handle
	constexpr bool fcb = true;
	uint16_t handle    = 0;
	ASSERT_TRUE(DOS_OpenFile("\\DEV\\CLOCK$", OPEN_READ, &handle, fcb));

	uint8_t record[6] = {};
	uint16_t amount   = 6;
	ASSERT_TRUE(DOS_ReadFile(handle, record, &amount, fcb));
	EXPECT_EQ(amount, 6);

	// Decode and compare against the live DOS date
	const auto days = static_cast<uint16_t>(record[0] | (record[1] << 8));
	const auto ymd  = std::chrono::year_month_day{
                std::chrono::sys_days{std::chrono::year{1980} /
                                      std::chrono::January /
                                      std::chrono::day{1}} +
                std::chrono::days{days}};

	EXPECT_EQ(static_cast<int>(ymd.year()), dos.date.year);
	EXPECT_EQ(static_cast<unsigned>(ymd.month()), dos.date.month);
	EXPECT_EQ(static_cast<unsigned>(ymd.day()), dos.date.day);

	EXPECT_LT(record[2], 60); // minutes
	EXPECT_LT(record[3], 24); // hours
	EXPECT_LT(record[4], 100); // hundredths
	EXPECT_LT(record[5], 60); // seconds

	// The record boundary signals one zero-byte read, which ends
	// sequential readers like TYPE instead of spinning them forever
	amount = 6;
	ASSERT_TRUE(DOS_ReadFile(handle, record, &amount, fcb));
	EXPECT_EQ(amount, 0);

	// Byte-wise reads must walk the whole record, not return the
	// first byte over and over
	uint8_t bytes[6] = {};
	for (auto i = 0; i < 6; ++i) {
		amount = 1;
		ASSERT_TRUE(DOS_ReadFile(handle, &bytes[i], &amount, fcb));
		EXPECT_EQ(amount, 1);
	}
	const auto bytewise_days = static_cast<uint16_t>(bytes[0] |
	                                                 (bytes[1] << 8));
	EXPECT_EQ(bytewise_days, days);

	ASSERT_TRUE(DOS_CloseFile(handle, fcb));
}

TEST_F(DOS_FilesTest, DOS_Clock_Device_Write_Read_Roundtrip)
{
	constexpr bool fcb = true;
	uint16_t handle    = 0;
	ASSERT_TRUE(DOS_OpenFile("CLOCK$", OPEN_READWRITE, &handle, fcb));

	// 1995-07-15 = 5674 days after 1980-01-01; 12:34:56.00
	const uint8_t set_record[6] = {
	        static_cast<uint8_t>(5674 & 0xff),
	        static_cast<uint8_t>(5674 >> 8),
	        34, // minutes
	        12, // hours
	        0,  // hundredths
	        56, // seconds
	};
	uint16_t amount = 6;
	ASSERT_TRUE(DOS_WriteFile(handle, const_cast<uint8_t*>(set_record), &amount, fcb));

	EXPECT_EQ(dos.date.year, 1995);
	EXPECT_EQ(dos.date.month, 7);
	EXPECT_EQ(dos.date.day, 15);

	uint8_t record[6] = {};
	amount            = 6;
	ASSERT_TRUE(DOS_ReadFile(handle, record, &amount, fcb));
	EXPECT_EQ(amount, 6);

	const auto days = static_cast<uint16_t>(record[0] | (record[1] << 8));
	EXPECT_EQ(days, 5674);

	// The time survives a trip through BIOS timer ticks (54.9 ms
	// each). Setting uses 1573040 ticks/day, reading uses
	// PIT_TICK_RATE/65536 ticks/s (INT 21h/2Dh vs 2Ch constants);
	// the constants diverge by ~1.5e-6, worth about a tick over
	// half a day, on top of the floor() quantization on each side.
	const int64_t set_hundredths = ((12 * 3600 + 34 * 60 + 56) * 100LL);
	const int64_t got_hundredths = ((record[3] * 3600LL + record[2] * 60LL +
                                        record[5]) * 100LL) + record[4];
	EXPECT_NEAR(static_cast<double>(got_hundredths),
	            static_cast<double>(set_hundredths), 20.0);

	// An out-of-range record must be rejected
	const uint8_t bad_record[6] = {0, 0, 77, 99, 0, 0};
	amount                      = 6;
	EXPECT_FALSE(DOS_WriteFile(handle, const_cast<uint8_t*>(bad_record), &amount, fcb));

	ASSERT_TRUE(DOS_CloseFile(handle, fcb));
}

TEST_F(DOS_FilesTest, DOS_FindDevice_Dev_Prefix_Negative)
{
	// not a device, even with the prefix
	EXPECT_EQ(DOS_FindDevice("\\DEV\\NODEV"), DOS_DEVICES);
	// only the root-level \DEV\ form is virtual
	EXPECT_EQ(DOS_FindDevice("\\DEV\\SUB\\NUL"), DOS_DEVICES);
	// nonexistent directories other than \DEV\ still fail
	EXPECT_EQ(DOS_FindDevice("\\NODIR\\NUL"), DOS_DEVICES);
}

TEST_F(DOS_FilesTest, DOS_FindFirst_FindFile)
{
	dos.errorcode = DOSERR_NONE;
	EXPECT_TRUE(DOS_FindFirst("Z:\\AUTOEXEC.BAT", 0, false));
	EXPECT_EQ(dos.errorcode, DOSERR_NONE);
}

TEST_F(DOS_FilesTest, DOS_FindFirst_FindFile_Nonexistant)
{
	dos.errorcode = DOSERR_NONE;
	EXPECT_FALSE(DOS_FindFirst("Z:\\AUTOEXEC.NO", 0, false));
	EXPECT_EQ(dos.errorcode, DOSERR_NO_MORE_FILES);
}

TEST_F(DOS_FilesTest, DOS_FindFirst_SurvivesCodePageLoad)
{
	// Loading a real code page in this minimal environment (no video
	// BIOS init) used to write the screen font through the null INT
	// 10h ROM pointers, trampling the DOS data area at the bottom of
	// memory. The device chain head lives there, and DOS_FindFirst
	// walked the corrupt chain forever. This is what 'auto' layout
	// detection does on a German-locale host.
	uint16_t code_page = 850;
	DOS_LoadKeyboardLayout("de", code_page, {}, false);

	dos.errorcode = DOSERR_NONE;
	EXPECT_TRUE(DOS_FindFirst("Z:\\AUTOEXEC.BAT", 0, false));
	EXPECT_EQ(dos.errorcode, DOSERR_NONE);
}

TEST_F(DOS_FilesTest, DOS_DTAExtendName_Space_Pads)
{
	assert_DTAExtendName("1234.E  ", "1234    ", "E  ");
}

TEST_F(DOS_FilesTest, DOS_DTAExtendName_Enforces_8_3)
{
	assert_DTAExtendName("12345678ABCDEF.123ABCDE", "12345678", "123");
}

TEST_F(DOS_FilesTest, VFILE_Register)
{
	VFILE_Register("TEST", nullptr, 0, "/");
	EXPECT_FALSE(DOS_FindFirst("Z:\\TEST\\FILENA~1.TXT", 0, false));
	VFILE_Register("filename_1.txt", nullptr, 0, "/TEST/");
	EXPECT_TRUE(DOS_FindFirst("Z:\\TEST\\FILENA~1.TXT", 0, false));
	EXPECT_FALSE(DOS_FindFirst("Z:\\TEST\\FILENA~2.TXT", 0, false));
	VFILE_Register("filename_2.txt", nullptr, 0, "/TEST/");
	EXPECT_TRUE(DOS_FindFirst("Z:\\TEST\\FILENA~2.TXT", 0, false));
	EXPECT_FALSE(DOS_FindFirst("Z:\\TEST\\FILENA~3.TXT", 0, false));
	VFILE_Register("filename_3.txt", nullptr, 0, "/TEST/");
	EXPECT_TRUE(DOS_FindFirst("Z:\\TEST\\FILENA~3.TXT", 0, false));
}

// Use shared_ptr instead of unique_ptr as the time cache depends on
// std::enable_shared_from_this which will matter for certains tests
static std::shared_ptr<localDrive> create_local_drive(const char* path)
{
	// I was not able to actually run MOUNT.COM inside the test enviornment.
	// These are the default parameters as set by the mount command.
	// They were retrieved from a debugger by setting a breakpoint inside the constructor.
	constexpr uint16_t BytesSector = 512;
	constexpr uint8_t SectorsCluster = 32;
	constexpr uint16_t TotalClusters = 32765;
	constexpr uint16_t FreeClusters = 16000;
	constexpr uint8_t MediaId = 248;
	constexpr bool ReadOnly = false;
	constexpr bool AlwaysOpenRoFiles = true;

	return std::make_shared<localDrive>(
		path,
		BytesSector,
		SectorsCluster,
		TotalClusters,
		FreeClusters,
		MediaId,
		ReadOnly,
		AlwaysOpenRoFiles
	);
}

TEST_F(DOS_FilesTest, SetDate_LocalDrive)
{
	const auto temp_handle = create_native_file("tests/files/paths/date.txt", {});
	ASSERT_NE(temp_handle, InvalidNativeFileHandle);
	close_native_file(temp_handle);

	auto local_drive = create_local_drive("tests/files/paths/");

	// Open read-only and test that we can still set the date.
	auto local_file = local_drive->FileOpen("date.txt", OPEN_READ);
	ASSERT_NE(local_file, nullptr);

	const auto time = DOS_PackTime(4, 20, 0);
	const auto date = DOS_PackDate(1995, 1, 1);

	// This simulates DOS_SetFileDate()
	// We can't actually call that function inside the test enviornment
	// because it relies on PSP enviornment variables.
	local_file->time = time;
	local_file->date = date;
	local_file->flush_time_on_close = FlushTimeOnClose::ManuallySet;

	local_file->Close();
	local_file.reset();
	local_drive.reset();

	// On close, the new time/date should be written out to the host filesystem.
	const auto native_handle = open_native_file("tests/files/paths/date.txt", false);
	ASSERT_NE(native_handle, InvalidNativeFileHandle);
	const auto date_time = get_dos_file_time(native_handle);
	ASSERT_EQ(date_time.date, date);
	ASSERT_EQ(date_time.time, time);
	ASSERT_TRUE(delete_native_file("tests/files/paths/date.txt"));
}

} // namespace
