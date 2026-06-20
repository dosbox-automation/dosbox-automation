// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_GUI_OSD_SDL_SHIM_H
#define DOSBOX_GUI_OSD_SDL_SHIM_H

#include <cstdint>

struct SDL_Renderer;

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

// Acquires the SDL_Renderer from GFX_GetWindow(). Returns nullptr
// when no SDL_Renderer is available (OpenGL backend).
SDL_Renderer* AcquireRenderer();

void DrawFilledRect(SDL_Renderer* r, const Rect& rect, const Color& color);

// Render a single VGA 8x16 glyph scaled by the given factor.
// Returns the pixel width consumed (8 * scale).
int DrawGlyph(SDL_Renderer* r, int x, int y, char ch, const Color& color, int scale);

// Render a string of VGA 8x16 glyphs. Returns total pixel width.
int DrawText(SDL_Renderer* r, int x, int y, const char* text,
             const Color& color, int scale);

// Screen dimensions of the current render target.
int OutputWidth(SDL_Renderer* r);
int OutputHeight(SDL_Renderer* r);

} // namespace OSD

#endif
