// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "hardware/audio/innovation.h"
#include "hardware/port.h"

#include <gtest/gtest.h>

#include "dosbox_test_fixture.h"

namespace {

constexpr io_val_t EmptyPortVal = 0xff;

constexpr io_val_t EntertainerIdValue = 0xa5;
constexpr io_port_t EntertainerIdPort = 0x200;

class HardwareAudioInnovationTest : public DOSBoxTestFixture{};

TEST_F(HardwareAudioInnovationTest, entertainer_uses_joystick_io_port)
{
	set_section_property_value("innovation", "innovation", "entertainer");
	INNOVATION_Init();
	EXPECT_EQ(IO_ReadB(EntertainerIdPort), EntertainerIdValue);

	INNOVATION_Destroy();
	EXPECT_EQ(IO_ReadB(EntertainerIdPort), EmptyPortVal);
}

TEST_F(HardwareAudioInnovationTest, innovation_no_joystick_io_port)
{
	set_section_property_value("innovation", "innovation", "on");
	INNOVATION_Init();
	EXPECT_NE(IO_ReadB(EntertainerIdPort), EntertainerIdValue);

	INNOVATION_Destroy();
	EXPECT_EQ(IO_ReadB(EntertainerIdPort), EmptyPortVal);
}


} // namespace
