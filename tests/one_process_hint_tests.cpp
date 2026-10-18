// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "one_process_hint.h"

#include <gtest/gtest.h>

namespace {

TEST(OneProcessHint, OneTestInTheProcessIsTheCtestCase)
{
	EXPECT_FALSE(OneProcessHint(1));
	EXPECT_FALSE(OneProcessHint(0));
}

TEST(OneProcessHint, MoreThanOneTestNamesTheCauseAndCtest)
{
	const auto hint = OneProcessHint(2315);
	ASSERT_TRUE(hint);
	EXPECT_NE(hint->find("2315"), std::string::npos);
	EXPECT_NE(hint->find("ctest"), std::string::npos);
	EXPECT_NE(hint->find("one process"), std::string::npos);
}

TEST(OneProcessHint, TwoTestsAreAlreadyAShortSuiteFilter)
{
	EXPECT_TRUE(OneProcessHint(2));
}

} // namespace
