// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "hardware/video/vga.h"

#include <cstdint>
#include <utility>

#include <gtest/gtest.h>

#include "dosbox_test_fixture.h"
#include "hardware/port.h"
#include "hardware/memory.h"
#include "ints/int10.h"

namespace {

// ada-8uii (GitHub #30): demos switch between 16 and 256 colours through
// the registers, without a BIOS mode set; VGA_SetMode is that switch.
class VgaModeSwitch
        : public DOSBoxTestFixture,
          public ::testing::WithParamInterface<std::pair<VGAModes, VGAModes>> {
protected:
	void SetUp() override
	{
		DOSBoxTestFixture::SetUp();
		VGA_Init();
		INT10_SetVideoMode(0x12);
		ASSERT_EQ(vga.mode, M_EGA);
	}
	void TearDown() override
	{
		VGA_Destroy();
		DOSBoxTestFixture::TearDown();
	}
};

// The planes are the only video memory; the 16-colour pixel buffer has to
// show what 256-colour writes left in them.
TEST_P(VgaModeSwitch, BackInSixteenColoursThePixelsComeFromThePlanes)
{
	const auto [sixteen, two_hundred_fifty_six] = GetParam();
	VGA_SetMode(two_hundred_fifty_six);

	// What a 256-colour write leaves: the planes change, nothing else.
	// Colour 5 in all eight pixels.
	vga.mem.linear[0] = 0xff;
	vga.mem.linear[1] = 0x00;
	vga.mem.linear[2] = 0xff;
	vga.mem.linear[3] = 0x00;

	VGA_SetMode(sixteen);

	for (int pixel = 0; pixel < 8; ++pixel) {
		EXPECT_EQ(vga.fastmem[pixel], 5) << "pixel " << pixel;
	}
}

// The DAC is the only palette; an entry set while 16 colours were showing
// is the colour 256-colour mode draws with.
TEST_P(VgaModeSwitch, InTwoHundredFiftySixColoursTheWholeDacIsShown)
{
	const auto [sixteen, two_hundred_fifty_six] = GetParam();
	VGA_SetMode(sixteen);

	constexpr uint8_t Entry = 40;
	IO_WriteB(0x3c8, Entry);
	IO_WriteB(0x3c9, 0x3f);
	IO_WriteB(0x3c9, 0x00);
	IO_WriteB(0x3c9, 0x00);

	VGA_SetMode(two_hundred_fifty_six);

	const auto colour = vga.dac.palette_map[Entry];
	EXPECT_EQ(colour.Red(), 0xff);
	EXPECT_EQ(colour.Green(), 0x00);
	EXPECT_EQ(colour.Blue(), 0x00);
}

// Chained mode 13h draws from a copy of the planes in fastmem; a trip
// through 16 colours must not leave the expanded pixels in it.
TEST_F(VgaModeSwitch, ModeThirteenSurvivesATripThroughSixteenColours)
{
	INT10_SetVideoMode(0x13);
	ASSERT_EQ(vga.mode, M_VGA);
	constexpr PhysPt Pixel = 100;
	constexpr uint8_t Colour = 0x2a;
	mem_writeb(0xa0000 + Pixel, Colour);
	ASSERT_EQ(vga.fastmem[Pixel], Colour);

	VGA_SetMode(M_EGA);
	VGA_SetMode(M_VGA);

	EXPECT_EQ(vga.fastmem[Pixel], Colour);
	EXPECT_EQ(vga.fastmem[64 * 1024 + Pixel], Colour) << "first line replica";
}

// The VESA modes draw from the same pixel buffer and DAC as the standard ones.
INSTANTIATE_TEST_SUITE_P(StandardAndVesa, VgaModeSwitch,
                         ::testing::Values(std::pair{M_EGA, M_VGA},
                                           std::pair{M_LIN4, M_LIN8}));

} // namespace
