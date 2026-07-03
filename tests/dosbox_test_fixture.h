// SPDX-FileCopyrightText:  2024-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_TEST_FIXTURE_H
#define DOSBOX_TEST_FIXTURE_H

#include <iterator>
#include <string>

#include <gtest/gtest.h>

#include "config/config.h"
#include "cpu/cpu.h"
#include "dos/dos.h"
#include "dosbox.h"
#include "hardware/serialport/serialport.h"
#include "ints/bios.h"
#include "shell/autoexec.h"

class DOSBoxTestFixture : public ::testing::Test {
public:
	// argv[0] is consumed as the program name, so switches must be
	// separate elements. The old single-string form was parsed as a
	// name and zero arguments, which silently loaded the developer's
	// primary config instead of the tests config. -noprimaryconf and
	// -nolocalconf keep host configs out of the test environment.
	DOSBoxTestFixture()
	        : argv{"dosbox_tests",
	               "-noprimaryconf",
	               "-nolocalconf",
	               "-conf",
	               "tests/files/dosbox-automation-tests.conf"},
	          command_line(5, argv)
	{
		control = std::make_unique<Config>(&command_line);
	}

	void SetUp() override
	{
		// Create the config directory, which is a
		// pre-requisite that's asserted during the Init process.
		//
		init_config_dir();
		const auto config_path = get_config_dir();

		// Register module config sections before parsing config
		// files; values parsed into unregistered sections are
		// silently dropped.
		//
		// Only initialiasing the minimum number of modules required for
		// the tests.
		//
		// This results in a 4-fold reduction in test execution times
		// compared to using `DOSBOX_InitModules()` (e.g. DOS_FilesTest
		// runs in 3 seconds instead of 13).
		//
		DOSBOX_InitModuleConfigsAndMessages();

		control->ParseConfigFiles(config_path);

		DOSBOX_Init();
		CPU_Init();
		BIOS_Init();
		SERIAL_Init();
		DOS_Init();
		AUTOEXEC_Init();
	}

	void TearDown() override
	{
		DOS_Destroy();
		SERIAL_Destroy();
		BIOS_Destroy();
		CPU_Destroy();
		DOSBOX_Destroy();

		control = {};
	}

private:
	const char* argv[5];

	CommandLine command_line;
	ConfigPtr config;
};

#endif
