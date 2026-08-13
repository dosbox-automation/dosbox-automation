// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "gui/osd/sdl_draw.h"

#include <cassert>

#include <SDL3/SDL.h>

#include "utils/checks.h"

CHECK_NARROWING();

namespace OSD {

SdlDrawContext::SdlDrawContext(SDL_Renderer* renderer) : renderer(renderer)
{
	assert(renderer);

	// The OSD background box is translucent; blend mode persists on the
	// renderer, so setting it once here covers all FillRect calls.
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
}

void SdlDrawContext::FillRect(const Rect& rect, const Color& color)
{
	if (!renderer) {
		return;
	}

	SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

	// SDL3 render geometry is float
	SDL_FRect sdl_rect = {static_cast<float>(rect.x),
	                      static_cast<float>(rect.y),
	                      static_cast<float>(rect.w),
	                      static_cast<float>(rect.h)};
	SDL_RenderFillRect(renderer, &sdl_rect);
}

int SdlDrawContext::OutputWidth()
{
	if (!renderer) {
		return 0;
	}
	int w = 0;
	int h = 0;
	SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
	return w;
}

int SdlDrawContext::OutputHeight()
{
	if (!renderer) {
		return 0;
	}
	int w = 0;
	int h = 0;
	SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
	return h;
}

} // namespace OSD
