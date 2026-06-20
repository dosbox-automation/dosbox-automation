// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "gui/osd/sdl_shim.h"

#include <SDL.h>

#include "gui/private/common.h"

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

SDL_Renderer* AcquireRenderer()
{
	auto* window = GFX_GetWindow();
	if (!window) {
		return nullptr;
	}
	return SDL_GetRenderer(window);
}

void DrawFilledRect(SDL_Renderer* r, const Rect& rect, const Color& color)
{
	if (!r) {
		return;
	}
	SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);
	SDL_Rect sdl_rect = {rect.x, rect.y, rect.w, rect.h};
	SDL_RenderFillRect(r, &sdl_rect);
}

int DrawGlyph(SDL_Renderer* r, const int x, const int y, const char ch,
              const Color& color, const int scale)
{
	if (!r) {
		return 0;
	}

	const auto idx        = static_cast<unsigned char>(ch);
	const uint8_t* bitmap = &int10_font_16[idx * 16];

	SDL_SetRenderDrawColor(r, color.r, color.g, color.b, color.a);

	for (int row = 0; row < 16; ++row) {
		const uint8_t bits = bitmap[row];
		for (int col = 0; col < 8; ++col) {
			if (bits & (0x80 >> col)) {
				SDL_Rect pixel = {x + col * scale,
				                  y + row * scale,
				                  scale,
				                  scale};
				SDL_RenderFillRect(r, &pixel);
			}
		}
	}

	return 8 * scale;
}

int DrawText(SDL_Renderer* r, const int x, const int y, const char* text,
             const Color& color, const int scale)
{
	if (!text || !r) {
		return 0;
	}

	int cursor_x = x;
	while (*text) {
		cursor_x += DrawGlyph(r, cursor_x, y, *text, color, scale);
		++text;
	}

	return cursor_x - x;
}

int OutputWidth(SDL_Renderer* r)
{
	if (!r) {
		return 0;
	}
	int w = 0;
	int h = 0;
	SDL_GetRendererOutputSize(r, &w, &h);
	return w;
}

int OutputHeight(SDL_Renderer* r)
{
	if (!r) {
		return 0;
	}
	int w = 0;
	int h = 0;
	SDL_GetRendererOutputSize(r, &w, &h);
	return h;
}

} // namespace OSD
