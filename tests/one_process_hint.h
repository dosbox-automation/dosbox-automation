// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_TESTS_ONE_PROCESS_HINT_H
#define DOSBOX_TESTS_ONE_PROCESS_HINT_H

#include <optional>
#include <string>

// Suites share static state (the mount policy's lock, SDL, the DOS kernel)
// and find fixtures per test, so more than one test in a process fails
// tests that pass alone; ctest runs one per process, a bare run does not.
std::optional<std::string> OneProcessHint(int selected_tests);

#endif
