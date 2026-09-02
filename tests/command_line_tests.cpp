// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "shell/command_line.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>

namespace {

TEST(CommandLine, find_remove_option_takes_the_following_token_as_value)
{
	CommandLine cmd("MOUNT", "C img -label GAME -ro");

	std::optional<std::string> value = {};
	EXPECT_EQ(cmd.FindRemoveOption("-label", value), 1);
	ASSERT_TRUE(value.has_value());
	EXPECT_EQ(*value, "GAME");

	std::string rest = {};
	EXPECT_TRUE(cmd.FindCommand(3, rest));
	EXPECT_EQ(rest, "-ro");
	EXPECT_FALSE(cmd.FindCommand(4, rest));
}

TEST(CommandLine, find_remove_option_never_takes_a_dash_token_as_value)
{
	CommandLine cmd("MOUNT", "C img -ide -label GAME");

	std::optional<std::string> value = {};
	EXPECT_EQ(cmd.FindRemoveOption("-ide", value), 1);
	EXPECT_FALSE(value.has_value());

	std::string rest = {};
	ASSERT_TRUE(cmd.FindCommand(3, rest));
	EXPECT_EQ(rest, "-label");
}

TEST(CommandLine, find_remove_option_at_the_end_has_no_value)
{
	CommandLine cmd("MOUNT", "C img -ide");

	std::optional<std::string> value = {};
	EXPECT_EQ(cmd.FindRemoveOption("-ide", value), 1);
	EXPECT_FALSE(value.has_value());
	EXPECT_EQ(cmd.GetCount(), 2u);
}

TEST(CommandLine, find_remove_option_counts_and_consumes_every_repeat)
{
	CommandLine cmd("MOUNT", "C -size 1,2,3,4 img -size 5,6,7,8");

	std::optional<std::string> value = {};
	EXPECT_EQ(cmd.FindRemoveOption("-size", value), 2);
	ASSERT_TRUE(value.has_value());
	EXPECT_EQ(*value, "1,2,3,4");
	EXPECT_EQ(cmd.GetCount(), 2u);
}

TEST(CommandLine, find_remove_option_reports_zero_when_absent)
{
	CommandLine cmd("MOUNT", "C img");

	std::optional<std::string> value = {};
	EXPECT_EQ(cmd.FindRemoveOption("-size", value), 0);
	EXPECT_FALSE(value.has_value());
	EXPECT_EQ(cmd.GetCount(), 2u);
}

TEST(CommandLine, find_remove_option_matches_case_insensitively)
{
	CommandLine cmd("MOUNT", "C img -LABEL GAME");

	std::optional<std::string> value = {};
	EXPECT_EQ(cmd.FindRemoveOption("-label", value), 1);
	ASSERT_TRUE(value.has_value());
	EXPECT_EQ(*value, "GAME");
}

} // namespace
