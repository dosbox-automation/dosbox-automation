// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "gui/osd/osd.h"

#include <algorithm>

namespace OSD {

OsdManager& OsdManager::Instance()
{
	static OsdManager instance;
	return instance;
}

void OsdManager::ShowText(TextOverlay overlay)
{
	if (overlay.text.size() > MaxTextLen) {
		overlay.text.resize(MaxTextLen);
	}

	if (!overlay.tag.empty()) {
		for (auto& existing : overlays) {
			if (existing.tag == overlay.tag) {
				existing = overlay;
				return;
			}
		}
	}

	if (overlays.size() >= MaxOverlays) {
		return;
	}
	overlays.push_back(std::move(overlay));
}

void OsdManager::ClearByTag(const std::string& tag)
{
	overlays.erase(std::remove_if(overlays.begin(),
	                              overlays.end(),
	                              [&tag](const TextOverlay& o) {
		                              return o.tag == tag;
	                              }),
	               overlays.end());
}

void OsdManager::ClearAll()
{
	overlays.clear();
}

void OsdManager::SetIcon(const IconId id, const bool active)
{
	const auto idx = static_cast<int>(id);
	if (idx >= 0 && idx < MaxIcons) {
		icons[idx].active   = active;
		icons[idx].blinking = active;
	}
}

void OsdManager::PruneExpired(const uint64_t frame_number)
{
	overlays.erase(std::remove_if(overlays.begin(),
	                              overlays.end(),
	                              [frame_number](const TextOverlay& o) {
		                              return o.expire_frame >= 0 &&
		                                     static_cast<uint64_t>(
		                                             o.expire_frame) <=
		                                             frame_number;
	                              }),
	               overlays.end());
}

void OsdManager::RenderOverlays(DrawContext& ctx, const uint64_t frame_number)
{
	if (overlays.empty()) {
		return;
	}

	const int screen_w = ctx.OutputWidth();
	const int screen_h = ctx.OutputHeight();

	constexpr int padding = 8;

	int top_left_y    = padding;
	int bottom_left_y = screen_h - padding;

	for (const auto& o : overlays) {
		if (o.expire_frame >= 0 &&
		    static_cast<uint64_t>(o.expire_frame) <= frame_number) {
			continue;
		}

		const int scale   = static_cast<int>(o.size);
		const int glyph_w = 8 * scale;
		const int glyph_h = 16 * scale;
		const int text_w  = static_cast<int>(o.text.size()) * glyph_w;

		int x = padding;
		int y = top_left_y;

		switch (o.position) {
		case Position::TopLeft:
			x = padding;
			y = top_left_y;
			top_left_y += glyph_h + 4;
			break;
		case Position::TopRight:
			x = screen_w - text_w - padding;
			y = padding;
			break;
		case Position::BottomLeft:
			bottom_left_y -= glyph_h;
			x = padding;
			y = bottom_left_y;
			bottom_left_y -= 4;
			break;
		case Position::BottomCenter:
			x = (screen_w - text_w) / 2;
			y = screen_h - glyph_h - padding;
			break;
		case Position::Custom:
			x = o.custom_x;
			y = o.custom_y;
			break;
		}

		Color bg     = {0, 0, 0, 96};
		Rect bg_rect = {x - 2, y - 2, text_w + 4, glyph_h + 4};
		ctx.FillRect(bg_rect, bg);

		DrawText(ctx, x, y, o.text.c_str(), o.color, scale);
	}
}

void OsdManager::RenderIcons(DrawContext& ctx, const uint64_t frame_number)
{
	const int screen_w = ctx.OutputWidth();

	constexpr int icon_size      = 12;
	constexpr int gap            = 4;
	constexpr int padding        = 8;
	constexpr int blink_interval = 30;

	int x = screen_w - padding - icon_size;

	for (const auto& icon : icons) {
		if (!icon.active) {
			continue;
		}

		bool visible = true;
		if (icon.blinking) {
			const auto phase = (frame_number / blink_interval) % 2;
			visible          = (phase == 0);
		}

		if (visible) {
			Color color;
			switch (icon.id) {
			case IconId::ScriptRunning: color = ColorGreen(); break;
			case IconId::RecordingActive: color = ColorRed(); break;
			case IconId::ReplayActive: color = ColorYellow(); break;
			case IconId::ProgrammaticInput:
				color = ColorCyan();
				break;
			case IconId::Count: break;
			}

			Rect rect = {x, padding, icon_size, icon_size};
			ctx.FillRect(rect, color);
		}

		x -= (icon_size + gap);
	}
}

void OsdManager::ShowCommand(const std::string& command, const uint64_t current_frame)
{
	TextOverlay overlay;
	overlay.text         = command;
	overlay.color        = ColorGreen();
	overlay.position     = Position::TopLeft;
	overlay.size         = FontSize::Medium;
	overlay.expire_frame = static_cast<int64_t>(current_frame) + 90;
	overlay.tag          = "lua-cmd";
	ShowText(std::move(overlay));
}

void OsdManager::SetEnabled(const bool on)
{
	enabled = on;
}

bool OsdManager::IsEnabled() const
{
	return enabled;
}

void OsdManager::Render(const uint64_t frame_number, DrawContext& ctx)
{
	if (!enabled) {
		return;
	}

	PruneExpired(frame_number);
	RenderOverlays(ctx, frame_number);
	RenderIcons(ctx, frame_number);
}

} // namespace OSD

void OSD_Render(const uint64_t frame_number, OSD::DrawContext& ctx)
{
	OSD::OsdManager::Instance().Render(frame_number, ctx);
}

void OSD_ShowCommand(const std::string& command, const uint64_t frame)
{
	OSD::OsdManager::Instance().ShowCommand(command, frame);
}

void OSD_ClearCommand()
{
	OSD::OsdManager::Instance().ClearByTag("lua-cmd");
}
