// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "gui/osd/draw_context.h"

#include "utils/checks.h"

CHECK_NARROWING();

// VGA ROM font - 256 glyphs, 16 bytes each, one bit per pixel, MSB left.
extern uint8_t int10_font_16[256 * 16];

namespace OSD {

Color ColorWhite()
{
	return {255, 255, 255, 255};
}
Color ColorGreen()
{
	return {0, 255, 0, 255};
}
Color ColorYellow()
{
	return {255, 255, 0, 255};
}
Color ColorRed()
{
	return {255, 0, 0, 255};
}
Color ColorCyan()
{
	return {0, 255, 255, 255};
}

int DrawGlyph(DrawContext& ctx, const int x, const int y, const char ch,
              const Color& color, const int scale)
{
	if (scale < 1) {
		return 0;
	}

	const auto idx        = static_cast<unsigned char>(ch);
	const uint8_t* bitmap = &int10_font_16[idx * 16];

	// Emit one rect per horizontal run of set bits instead of one per
	// pixel; same coverage, far fewer rects for both backends.
	for (int row = 0; row < 16; ++row) {
		const uint8_t bits = bitmap[row];

		int col = 0;
		while (col < 8) {
			if (!(bits & (0x80 >> col))) {
				++col;
				continue;
			}

			const int run_start = col;
			while (col < 8 && (bits & (0x80 >> col))) {
				++col;
			}
			const int run_len = col - run_start;

			const Rect run = {x + run_start * scale,
			                  y + row * scale,
			                  run_len * scale,
			                  scale};
			ctx.FillRect(run, color);
		}
	}

	return 8 * scale;
}

int DrawText(DrawContext& ctx, const int x, const int y, const char* text,
             const Color& color, const int scale)
{
	if (!text) {
		return 0;
	}

	int cursor_x = x;
	while (*text) {
		cursor_x += DrawGlyph(ctx, cursor_x, y, *text, color, scale);
		++text;
	}

	return cursor_x - x;
}

} // namespace OSD
