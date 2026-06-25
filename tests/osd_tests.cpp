// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "gui/osd/osd.h"

#include <gtest/gtest.h>

// The OSD is the on-screen proof that automation is driving the machine, so it
// must be on by default. webserver_osd=false flips it off via SetEnabled.
TEST(Osd, EnabledByDefault)
{
	auto& osd = OSD::OsdManager::Instance();
	EXPECT_TRUE(osd.IsEnabled());
}

TEST(Osd, ToggleRoundTrips)
{
	auto& osd = OSD::OsdManager::Instance();

	osd.SetEnabled(false);
	EXPECT_FALSE(osd.IsEnabled());

	osd.SetEnabled(true);
	EXPECT_TRUE(osd.IsEnabled());
}
