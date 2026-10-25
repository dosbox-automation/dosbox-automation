// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "dos/programs/doskey.h"

#include <gtest/gtest.h>

#include <string>
#include <variant>
#include <vector>

using namespace DoskeyCommand;

namespace {

Request parse_ok(const std::vector<std::string>& args)
{
	const auto result = ParseArguments(args);
	if (const auto error = std::get_if<IllegalSwitch>(&result)) {
		ADD_FAILURE() << "unexpected illegal switch '"
		              << error->argument << "'";
		return {};
	}
	return std::get<Request>(result);
}

std::string illegal(const std::vector<std::string>& args)
{
	const auto result = ParseArguments(args);
	const auto error  = std::get_if<IllegalSwitch>(&result);
	return error ? error->argument : std::string("(accepted)");
}

} // namespace

TEST(DoskeyParse, nothing_installs)
{
	const auto request = parse_ok({});
	EXPECT_FALSE(request.show_history);
	EXPECT_FALSE(request.show_macros);
	EXPECT_FALSE(request.macro_text);
}

// AUTOEXEC.BAT files of the time pass these; the shell's own line editor
// makes them moot, so they are taken and change nothing.
TEST(DoskeyParse, the_settings_switches_are_taken_in_any_case)
{
	const auto request = parse_ok(
	        {"/reinstall", "/BUFSIZE=1024", "/Insert", "/overstrike"});
	EXPECT_FALSE(request.show_history);
	EXPECT_FALSE(request.macro_text);
}

TEST(DoskeyParse, history_and_macros_have_short_forms)
{
	EXPECT_TRUE(parse_ok({"/history"}).show_history);
	EXPECT_TRUE(parse_ok({"/H"}).show_history);
	EXPECT_TRUE(parse_ok({"/macros"}).show_macros);
	EXPECT_TRUE(parse_ok({"/m"}).show_macros);
}

TEST(DoskeyParse, a_word_that_is_no_switch_starts_a_macro)
{
	const auto request = parse_ok({"ls=dir", "$*", "/w"});
	ASSERT_TRUE(request.macro_text);
	EXPECT_EQ(*request.macro_text, "ls=dir $* /w");
}

TEST(DoskeyParse, unknown_switches_and_a_bufsize_without_a_number_are_refused)
{
	EXPECT_EQ(illegal({"/x"}), "/x");
	EXPECT_EQ(illegal({"/bufsize"}), "/bufsize");
	EXPECT_EQ(illegal({"/bufsize=big"}), "/bufsize=big");
	EXPECT_EQ(illegal({"/history", "/q"}), "/q");
}
