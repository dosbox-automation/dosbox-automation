// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "one_process_hint.h"

#include <gtest/gtest.h>

#include <cstdio>

std::optional<std::string> OneProcessHint(const int selected_tests)
{
	if (selected_tests <= 1) {
		return {};
	}
	return "dosbox_tests: " + std::to_string(selected_tests) +
	       " tests in one process. The suites share static state and "
	       "find their fixtures per test, so a combined run fails tests "
	       "that pass alone. Run `ctest` from the build directory instead.\n";
}

namespace {

class OneProcessHintEnvironment : public ::testing::Environment {
public:
	void SetUp() override
	{
		const auto* unit_test = ::testing::UnitTest::GetInstance();
		if (const auto hint = OneProcessHint(unit_test->test_to_run_count())) {
			std::fputs(hint->c_str(), stderr);
		}
	}
};

// gtest owns the environment; the static keeps the registration out of main
[[maybe_unused]] const ::testing::Environment* const one_process_hint_env =
        ::testing::AddGlobalTestEnvironment(new OneProcessHintEnvironment);

} // namespace
