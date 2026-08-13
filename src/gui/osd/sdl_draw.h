// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_GUI_OSD_SDL_DRAW_H
#define DOSBOX_GUI_OSD_SDL_DRAW_H

#include "gui/osd/draw_context.h"

struct SDL_Renderer;

namespace OSD {

// Draws the OSD through an SDL_Renderer (the texture output backend).
// Cheap to construct; the SDL backend creates one per presented frame.
class SdlDrawContext final : public DrawContext {
public:
	explicit SdlDrawContext(SDL_Renderer* renderer);

	void FillRect(const Rect& rect, const Color& color) override;

	int OutputWidth() override;
	int OutputHeight() override;

	// prevent copying
	SdlDrawContext(const SdlDrawContext&)            = delete;
	SdlDrawContext& operator=(const SdlDrawContext&) = delete;

private:
	SDL_Renderer* renderer = nullptr;
};

} // namespace OSD

#endif
