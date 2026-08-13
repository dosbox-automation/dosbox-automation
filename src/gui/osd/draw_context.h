// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_GUI_OSD_DRAW_CONTEXT_H
#define DOSBOX_GUI_OSD_DRAW_CONTEXT_H

#include <cstdint>

namespace OSD {

struct Rect {
	int x = 0;
	int y = 0;
	int w = 0;
	int h = 0;
};

struct Color {
	uint8_t r = 255;
	uint8_t g = 255;
	uint8_t b = 255;
	uint8_t a = 255;
};

Color ColorWhite();
Color ColorGreen();
Color ColorYellow();
Color ColorRed();
Color ColorCyan();

// Backend-neutral draw target for the OSD. The whole OSD is drawn as
// filled rectangles (glyphs are expanded from the VGA ROM font bitmap),
// so this is the entire contract a render backend has to satisfy.
class DrawContext {
public:
	virtual ~DrawContext() = default;

	virtual void FillRect(const Rect& rect, const Color& color) = 0;

	// Size of the drawable window area in pixels.
	virtual int OutputWidth()  = 0;
	virtual int OutputHeight() = 0;
};

// Render a single VGA 8x16 glyph scaled by the given factor.
// Returns the pixel width consumed (8 * scale).
int DrawGlyph(DrawContext& ctx, int x, int y, char ch, const Color& color, int scale);

// Render a string of VGA 8x16 glyphs. Returns total pixel width.
int DrawText(DrawContext& ctx, int x, int y, const char* text,
             const Color& color, int scale);

} // namespace OSD

#endif
