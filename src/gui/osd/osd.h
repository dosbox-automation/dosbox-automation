// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_GUI_OSD_H
#define DOSBOX_GUI_OSD_H

#include "gui/osd/sdl_shim.h"

#include <cstdint>
#include <string>
#include <vector>

namespace OSD {

enum class Position { TopLeft, TopRight, BottomLeft, BottomCenter, Custom };

enum class FontSize { Small = 1, Medium = 2, Large = 3 };

struct TextOverlay {
	std::string text     = {};
	Color color          = ColorGreen();
	Position position    = Position::TopLeft;
	FontSize size        = FontSize::Medium;
	int custom_x         = 0;
	int custom_y         = 0;
	int64_t expire_frame = -1;
	std::string tag      = {};
};

enum class IconId {
	ScriptRunning,
	RecordingActive,
	ReplayActive,
	ProgrammaticInput,
	Count
};

class OsdManager {
public:
	static OsdManager& Instance();

	void ShowText(TextOverlay overlay);
	void ClearByTag(const std::string& tag);
	void ClearAll();

	void SetIcon(IconId id, bool active);

	// Master switch. On by default so automation activity is always visible;
	// set false (webserver_osd=false) to suppress the overlay entirely.
	void SetEnabled(bool on);
	bool IsEnabled() const;

	void Render(uint64_t frame_number);

private:
	OsdManager() = default;

	static constexpr int MaxIcons       = static_cast<int>(IconId::Count);
	static constexpr size_t MaxOverlays = 32;
	static constexpr size_t MaxTextLen  = 256;

	struct StatusIcon {
		IconId id     = IconId::ScriptRunning;
		bool active   = false;
		bool blinking = false;
	};

	bool enabled = true;

	std::vector<TextOverlay> overlays = {};
	StatusIcon icons[MaxIcons]        = {
                {    IconId::ScriptRunning, false, false},
                {  IconId::RecordingActive, false, false},
                {     IconId::ReplayActive, false, false},
                {IconId::ProgrammaticInput, false, false},
        };

	void RenderOverlays(SDL_Renderer* r, uint64_t frame_number);
	void RenderIcons(SDL_Renderer* r, uint64_t frame_number);
	void PruneExpired(uint64_t frame_number);

	OsdManager(const OsdManager&)            = delete;
	OsdManager& operator=(const OsdManager&) = delete;
};

} // namespace OSD

void OSD_Render(uint64_t frame_number);

#endif
