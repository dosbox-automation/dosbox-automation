// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "hardware/video/vga.h"

#include <gtest/gtest.h>

namespace {

// ada-5bth: PEL panning shifts the video data left (IBM Video Subsystem,
// VGA Function, 1992, figure 2-88); the line has to start earlier in the
// buffer, not later.
TEST(VgaTextPanning, UnpannedLineStartsAtTheLead)
{
	EXPECT_EQ(TextLineDrawStart(0), TextLineLeadPixels);
}

TEST(VgaTextPanning, PanningMovesTheStartLeft)
{
	EXPECT_EQ(TextLineDrawStart(3), TextLineLeadPixels - 3);
}

TEST(VgaTextPanning, TheWidestPanStartsAtTheBufferStart)
{
	// Eight dots is the most a 9-dot text mode pans.
	EXPECT_EQ(TextLineDrawStart(8), 0);
}

} // namespace
