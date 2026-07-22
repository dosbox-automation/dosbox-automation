// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include <gtest/gtest.h>

#include "dosbox_test_fixture.h"
#include "hardware/memory.h"
#include "ints/int10.h"

namespace {

// Cherry-picked from dosbox-staging PR #4959 (codengine):
// BDA cursor type (word at 0040:0060) must be cleared when
// switching to EGA/VGA graphics modes. Some TSRs check this
// word to distinguish text from graphics mode.

class BdaCursorType : public DOSBoxTestFixture {};

TEST_F(BdaCursorType, text_mode_sets_cursor_shape)
{
	// The fixture initializes subsystems but doesn't set a video
	// mode. Explicitly enter text mode 3 and verify the BDA gets
	// the standard underline cursor shape (0x0607).
	INT10_SetVideoMode(0x03);

	const auto cursor_type = real_readw(BIOSMEM_SEG, BIOSMEM_CURSOR_TYPE);
	EXPECT_EQ(cursor_type, 0x0607);
}

TEST_F(BdaCursorType, vga_graphics_mode_clears_cursor_type)
{
	// Set text mode to establish a cursor shape, then switch to
	// VGA mode 13h (320x200 256-color). The cursor type must be
	// cleared so TSRs can distinguish graphics from text mode.
	INT10_SetVideoMode(0x03);
	EXPECT_NE(real_readw(BIOSMEM_SEG, BIOSMEM_CURSOR_TYPE), 0);

	INT10_SetVideoMode(0x13);
	EXPECT_EQ(real_readw(BIOSMEM_SEG, BIOSMEM_CURSOR_TYPE), 0);
}

TEST_F(BdaCursorType, ega_graphics_mode_clears_cursor_type)
{
	// Same test for EGA mode 10h (640x350 16-color).
	INT10_SetVideoMode(0x03);
	EXPECT_NE(real_readw(BIOSMEM_SEG, BIOSMEM_CURSOR_TYPE), 0);

	INT10_SetVideoMode(0x10);
	EXPECT_EQ(real_readw(BIOSMEM_SEG, BIOSMEM_CURSOR_TYPE), 0);
}

TEST_F(BdaCursorType, back_to_text_restores_cursor_type)
{
	// Text -> graphics (clears) -> text (restores).
	INT10_SetVideoMode(0x03);
	EXPECT_NE(real_readw(BIOSMEM_SEG, BIOSMEM_CURSOR_TYPE), 0);

	INT10_SetVideoMode(0x13);
	EXPECT_EQ(real_readw(BIOSMEM_SEG, BIOSMEM_CURSOR_TYPE), 0);

	INT10_SetVideoMode(0x03);
	EXPECT_NE(real_readw(BIOSMEM_SEG, BIOSMEM_CURSOR_TYPE), 0);
}

} // namespace
