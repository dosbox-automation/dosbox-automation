// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "shell/shell_args.h"

#include <gtest/gtest.h>

using namespace ShellArgs;

TEST(ShellArgsOnOff, nothing_shows_the_state)
{
	EXPECT_EQ(ParseOnOff(""), OnOff::Show);
	EXPECT_EQ(ParseOnOff("  \t"), OnOff::Show);
}

TEST(ShellArgsOnOff, on_and_off_in_any_case)
{
	EXPECT_EQ(ParseOnOff(" on"), OnOff::On);
	EXPECT_EQ(ParseOnOff(" ON"), OnOff::On);
	EXPECT_EQ(ParseOnOff(" Off"), OnOff::Off);
	EXPECT_EQ(ParseOnOff(" off  "), OnOff::Off);
}

// COMMAND.COM reads comma, semicolon and equal sign as separators too.
TEST(ShellArgsOnOff, separators_before_the_word)
{
	EXPECT_EQ(ParseOnOff("=on"), OnOff::On);
	EXPECT_EQ(ParseOnOff(",off"), OnOff::Off);
	EXPECT_EQ(ParseOnOff(";on"), OnOff::On);
}

TEST(ShellArgsOnOff, anything_else_is_refused)
{
	EXPECT_EQ(ParseOnOff(" yes"), OnOff::Invalid);
	EXPECT_EQ(ParseOnOff(" onx"), OnOff::Invalid);
	EXPECT_EQ(ParseOnOff(" on off"), OnOff::Invalid);
	EXPECT_EQ(ParseOnOff(" 1"), OnOff::Invalid);
}

TEST(ShellArgsDevice, one_word_is_the_device)
{
	EXPECT_EQ(ParseDeviceName(" nul"), "nul");
	EXPECT_EQ(ParseDeviceName("\tCON  "), "CON");
	EXPECT_EQ(ParseDeviceName(" com1"), "com1");
}

TEST(ShellArgsDevice, nothing_or_two_words_is_no_device)
{
	EXPECT_FALSE(ParseDeviceName(""));
	EXPECT_FALSE(ParseDeviceName("   "));
	EXPECT_FALSE(ParseDeviceName(" con nul"));
}
