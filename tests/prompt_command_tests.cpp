// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "shell/prompt_command.h"

#include <gtest/gtest.h>

#include <string>

using namespace PromptCommand;

namespace {

Context SampleContext()
{
	Context context    = {};
	context.drive      = [] { return 'C'; };
	context.drive_path = [] { return std::string("C:\\GAMES"); };
	context.time       = [] { return std::string("7:05:09.12"); };
	context.date       = [] { return std::string("Sat 10-24-2026"); };
	context.version    = [] { return std::string("DOS version 5.00"); };
	return context;
}

} // namespace

TEST(PromptRender, the_classic_prompt_shows_path_and_greater_than)
{
	EXPECT_EQ(Render("$p$g", SampleContext()), "C:\\GAMES>");
}

TEST(PromptRender, codes_are_case_insensitive)
{
	EXPECT_EQ(Render("$P$G", SampleContext()), "C:\\GAMES>");
	EXPECT_EQ(Render("$N$g", SampleContext()), "C>");
}

TEST(PromptRender, every_character_code_of_the_manual)
{
	EXPECT_EQ(Render("$q$$$l$b$g", SampleContext()), "=$<|>");
	EXPECT_EQ(Render("$e[7m$n:$e[m", SampleContext()), "\x1b[7mC:\x1b[m");
}

TEST(PromptRender, time_date_and_version)
{
	EXPECT_EQ(Render("time is: $t$_date is: $d", SampleContext()),
	          "time is: 7:05:09.12\r\ndate is: Sat 10-24-2026");
	EXPECT_EQ(Render("$v", SampleContext()), "DOS version 5.00");
}

// $h is COMMAND.COM's back-space message: back, blank, back.
TEST(PromptRender, backspace_erases_the_character_before_it)
{
	EXPECT_EQ(Render("ab$h>", SampleContext()), "ab\b \b>");
}

// MS-DOS 4.0 TCMD2A.ASM print_prompt skips a code it does not know and
// stops at a $ with nothing after it.
TEST(PromptRender, unknown_codes_vanish_and_a_last_dollar_ends_the_text)
{
	EXPECT_EQ(Render("a$xb$1c", SampleContext()), "abc");
	EXPECT_EQ(Render("a$", SampleContext()), "a");
	EXPECT_EQ(Render("plain text>", SampleContext()), "plain text>");
}

TEST(PromptRender, a_value_is_asked_for_only_when_its_code_is_used)
{
	auto asked   = 0;
	auto context = SampleContext();
	context.time = [&asked] {
		++asked;
		return std::string("7:05");
	};
	context.date = [&asked] {
		++asked;
		return std::string("Sat");
	};
	EXPECT_EQ(Render("$p$g", context), "C:\\GAMES>");
	EXPECT_EQ(asked, 0);
}

TEST(PromptRender, a_missing_value_renders_as_nothing)
{
	EXPECT_EQ(Render("[$p$t]", Context{}), "[]");
}

TEST(PromptParse, the_text_after_blanks_tabs_and_one_equal_sign)
{
	EXPECT_EQ(ParseArguments(" $p$g"), "$p$g");
	EXPECT_EQ(ParseArguments("\t$n$g"), "$n$g");
	EXPECT_EQ(ParseArguments("=$p$g"), "$p$g");
	EXPECT_EQ(ParseArguments(" = $p$g"), "$p$g");
	EXPECT_EQ(ParseArguments("time is: $t"), "time is: $t");
}

TEST(PromptParse, no_text_means_the_default_prompt)
{
	EXPECT_EQ(ParseArguments(""), "");
	EXPECT_EQ(ParseArguments("   "), "");
	EXPECT_EQ(ParseArguments("="), "");
}

TEST(PromptParse, trailing_blanks_belong_to_the_prompt)
{
	EXPECT_EQ(ParseArguments(" $p$g "), "$p$g ");
}
