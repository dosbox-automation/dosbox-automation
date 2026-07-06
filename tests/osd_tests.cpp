// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "gui/osd/osd.h"

#include <gtest/gtest.h>

#include "gui/osd/gl_draw.h"

// VGA ROM font - 256 glyphs, 16 bytes each, one bit per pixel, MSB left.
extern uint8_t int10_font_16[256 * 16];

namespace {

struct RecordedRect {
	OSD::Rect rect   = {};
	OSD::Color color = {};
};

// Captures FillRect calls so OsdManager and the glyph expansion can be
// asserted against without any real render backend.
class RecordingDrawContext final : public OSD::DrawContext {
public:
	RecordingDrawContext(const int width, const int height)
	        : width(width),
	          height(height)
	{}

	void FillRect(const OSD::Rect& rect, const OSD::Color& color) override
	{
		rects.push_back({rect, color});
	}

	int OutputWidth() override
	{
		return width;
	}
	int OutputHeight() override
	{
		return height;
	}

	std::vector<RecordedRect> rects = {};

private:
	int width  = 0;
	int height = 0;
};

bool covers_pixel(const std::vector<RecordedRect>& rects, const int px, const int py)
{
	for (const auto& r : rects) {
		if (px >= r.rect.x && px < r.rect.x + r.rect.w &&
		    py >= r.rect.y && py < r.rect.y + r.rect.h) {
			return true;
		}
	}
	return false;
}

// The OsdManager is a singleton; every test starts from a known state.
void reset_osd()
{
	auto& osd = OSD::OsdManager::Instance();

	osd.SetEnabled(true);
	osd.ClearAll();

	osd.SetIcon(OSD::IconId::ScriptRunning, false);
	osd.SetIcon(OSD::IconId::RecordingActive, false);
	osd.SetIcon(OSD::IconId::ReplayActive, false);
	osd.SetIcon(OSD::IconId::ProgrammaticInput, false);
}

class Osd : public ::testing::Test {
protected:
	void SetUp() override
	{
		reset_osd();
	}
	void TearDown() override
	{
		reset_osd();
	}
};

// The OSD is the on-screen proof that automation is driving the machine, so it
// must be on by default. webserver_osd=false flips it off via SetEnabled.
TEST_F(Osd, EnabledByDefault)
{
	EXPECT_TRUE(OSD::OsdManager::Instance().IsEnabled());
}

TEST_F(Osd, ToggleRoundTrips)
{
	auto& osd = OSD::OsdManager::Instance();

	osd.SetEnabled(false);
	EXPECT_FALSE(osd.IsEnabled());

	osd.SetEnabled(true);
	EXPECT_TRUE(osd.IsEnabled());
}

TEST_F(Osd, DisabledRendersNothing)
{
	auto& osd = OSD::OsdManager::Instance();

	OSD::TextOverlay overlay;
	overlay.text = "HELLO";
	osd.ShowText(overlay);
	osd.SetIcon(OSD::IconId::ScriptRunning, true);

	osd.SetEnabled(false);

	RecordingDrawContext ctx(640, 480);
	osd.Render(0, ctx);

	EXPECT_TRUE(ctx.rects.empty());
}

TEST_F(Osd, OverlayDrawsBackgroundBoxThenText)
{
	auto& osd = OSD::OsdManager::Instance();

	OSD::TextOverlay overlay;
	overlay.text     = "AB";
	overlay.color    = OSD::ColorGreen();
	overlay.position = OSD::Position::TopLeft;
	overlay.size     = OSD::FontSize::Medium; // scale 2
	osd.ShowText(overlay);

	RecordingDrawContext ctx(640, 480);
	osd.Render(0, ctx);

	ASSERT_FALSE(ctx.rects.empty());

	// Background box: 2 px margin around 2 glyphs of 16x32 px at the
	// top-left padding offset, translucent black.
	const auto& bg = ctx.rects.front();
	EXPECT_EQ(bg.rect.x, 6);
	EXPECT_EQ(bg.rect.y, 6);
	EXPECT_EQ(bg.rect.w, 2 * 16 + 4);
	EXPECT_EQ(bg.rect.h, 32 + 4);
	EXPECT_EQ(bg.color.a, 96);

	// Everything after the box is glyph runs in the overlay color.
	ASSERT_GT(ctx.rects.size(), 1u);
	for (size_t i = 1; i < ctx.rects.size(); ++i) {
		EXPECT_EQ(ctx.rects[i].color.g, 255);
		EXPECT_EQ(ctx.rects[i].color.a, 255);
	}
}

TEST_F(Osd, TopRightOverlayIsRightAligned)
{
	auto& osd = OSD::OsdManager::Instance();

	OSD::TextOverlay overlay;
	overlay.text     = "XY";
	overlay.position = OSD::Position::TopRight;
	overlay.size     = OSD::FontSize::Small; // scale 1
	osd.ShowText(overlay);

	RecordingDrawContext ctx(640, 480);
	osd.Render(0, ctx);

	ASSERT_FALSE(ctx.rects.empty());

	constexpr auto text_w  = 2 * 8;
	constexpr auto padding = 8;

	const auto& bg = ctx.rects.front();
	EXPECT_EQ(bg.rect.x, 640 - text_w - padding - 2);
	EXPECT_EQ(bg.rect.y, padding - 2);
}

TEST_F(Osd, ExpiredOverlayIsSkippedAndPruned)
{
	auto& osd = OSD::OsdManager::Instance();

	OSD::TextOverlay overlay;
	overlay.text         = "GONE";
	overlay.expire_frame = 100;
	osd.ShowText(overlay);

	RecordingDrawContext before(640, 480);
	osd.Render(99, before);
	EXPECT_FALSE(before.rects.empty());

	RecordingDrawContext at_expiry(640, 480);
	osd.Render(100, at_expiry);
	EXPECT_TRUE(at_expiry.rects.empty());

	// Pruned, not just skipped: still nothing on a later frame.
	RecordingDrawContext after(640, 480);
	osd.Render(101, after);
	EXPECT_TRUE(after.rects.empty());
}

TEST_F(Osd, ShowCommandReplacesPreviousCommand)
{
	auto& osd = OSD::OsdManager::Instance();

	osd.ShowCommand("first", 0);
	osd.ShowCommand("second", 0);

	RecordingDrawContext ctx(640, 480);
	osd.Render(0, ctx);

	// One background box means one overlay; the tag replaced in place.
	size_t num_boxes = 0;
	for (const auto& r : ctx.rects) {
		if (r.color.a == 96) {
			++num_boxes;
		}
	}
	EXPECT_EQ(num_boxes, 1u);
}

TEST_F(Osd, ActiveIconBlinksOnFramePhase)
{
	auto& osd = OSD::OsdManager::Instance();
	osd.SetIcon(OSD::IconId::ScriptRunning, true);

	// Blink interval is 30 frames; phase 0 shows the icon.
	RecordingDrawContext visible(640, 480);
	osd.Render(0, visible);

	ASSERT_EQ(visible.rects.size(), 1u);

	const auto& icon = visible.rects.front();
	EXPECT_EQ(icon.rect.x, 640 - 8 - 12);
	EXPECT_EQ(icon.rect.y, 8);
	EXPECT_EQ(icon.rect.w, 12);
	EXPECT_EQ(icon.rect.h, 12);
	EXPECT_EQ(icon.color.g, 255);

	RecordingDrawContext hidden(640, 480);
	osd.Render(30, hidden);
	EXPECT_TRUE(hidden.rects.empty());
}

TEST_F(Osd, OverlayTextIsCappedAtMaxLength)
{
	auto& osd = OSD::OsdManager::Instance();

	OSD::TextOverlay overlay;
	overlay.text = std::string(300, 'W');
	overlay.size = OSD::FontSize::Small;
	osd.ShowText(overlay);

	RecordingDrawContext ctx(640, 480);
	osd.Render(0, ctx);

	ASSERT_FALSE(ctx.rects.empty());

	// 256-char cap reflected in the background box width.
	EXPECT_EQ(ctx.rects.front().rect.w, 256 * 8 + 4);
}

// --- Glyph expansion --------------------------------------------------------

TEST(OsdGlyph, CoverageMatchesFontBitmap)
{
	const unsigned char chars[] = {'A', '0', 0xDB, '!'};

	for (const auto ch : chars) {
		for (const int scale : {1, 2}) {
			RecordingDrawContext ctx(640, 480);

			const int advance = OSD::DrawGlyph(ctx,
			                                   0,
			                                   0,
			                                   static_cast<char>(ch),
			                                   OSD::ColorWhite(),
			                                   scale);
			EXPECT_EQ(advance, 8 * scale);

			const uint8_t* bitmap = &int10_font_16[ch * 16];

			for (int row = 0; row < 16; ++row) {
				for (int col = 0; col < 8; ++col) {
					const bool set = bitmap[row] & (0x80 >> col);
					const bool covered = covers_pixel(
					        ctx.rects, col * scale, row * scale);
					EXPECT_EQ(covered, set)
					        << "char " << static_cast<int>(ch)
					        << " scale " << scale << " row "
					        << row << " col " << col;
				}
			}
		}
	}
}

TEST(OsdGlyph, SpaceEmitsNoRectsButAdvances)
{
	RecordingDrawContext ctx(640, 480);

	const int advance = OSD::DrawGlyph(ctx, 0, 0, ' ', OSD::ColorWhite(), 2);

	EXPECT_EQ(advance, 16);
	EXPECT_TRUE(ctx.rects.empty());
}

TEST(OsdGlyph, RunsAreMergedPerRow)
{
	// 0xDB is the full block: 16 rows of 8 set bits each, so run
	// merging must emit exactly one rect per row.
	RecordingDrawContext ctx(640, 480);

	OSD::DrawGlyph(ctx, 0, 0, static_cast<char>(0xDB), OSD::ColorWhite(), 1);

	EXPECT_EQ(ctx.rects.size(), 16u);
	for (const auto& r : ctx.rects) {
		EXPECT_EQ(r.rect.w, 8);
		EXPECT_EQ(r.rect.h, 1);
	}
}

TEST(OsdGlyph, DrawTextAdvancesPerGlyph)
{
	RecordingDrawContext ctx(640, 480);

	const int width = OSD::DrawText(ctx, 10, 20, "AB", OSD::ColorWhite(), 1);

	EXPECT_EQ(width, 16);

	// No glyph pixel may land left of the string origin.
	for (const auto& r : ctx.rects) {
		EXPECT_GE(r.rect.x, 10);
		EXPECT_GE(r.rect.y, 20);
	}
}

TEST(OsdGlyph, RejectsInvalidScale)
{
	RecordingDrawContext ctx(640, 480);

	EXPECT_EQ(OSD::DrawGlyph(ctx, 0, 0, 'A', OSD::ColorWhite(), 0), 0);
	EXPECT_TRUE(ctx.rects.empty());
}

// --- OpenGL vertex generation -----------------------------------------------

#if C_OPENGL

constexpr auto NumFloatsPerRect = 6 * 6;

TEST(OsdGlVertices, FullWindowRectSpansNdc)
{
	std::vector<float> out = {};

	ASSERT_TRUE(OSD::AppendRectVertices(
	        out, {0, 0, 640, 480}, {255, 255, 255, 255}, 640, 480));

	ASSERT_EQ(out.size(), static_cast<size_t>(NumFloatsPerRect));

	// First vertex is the top-left corner: NDC (-1, 1) with y up.
	EXPECT_FLOAT_EQ(out[0], -1.0f);
	EXPECT_FLOAT_EQ(out[1], 1.0f);

	// Fifth vertex is the bottom-right corner: NDC (1, -1).
	constexpr auto v4 = 4 * 6;
	EXPECT_FLOAT_EQ(out[v4 + 0], 1.0f);
	EXPECT_FLOAT_EQ(out[v4 + 1], -1.0f);
}

TEST(OsdGlVertices, PixelOffsetsMapWithYFlip)
{
	std::vector<float> out = {};

	ASSERT_TRUE(OSD::AppendRectVertices(
	        out, {8, 8, 8, 16}, {255, 255, 255, 255}, 640, 480));

	EXPECT_FLOAT_EQ(out[0], 2.0f * 8.0f / 640.0f - 1.0f);
	EXPECT_FLOAT_EQ(out[1], 1.0f - 2.0f * 8.0f / 480.0f);

	constexpr auto v4 = 4 * 6;
	EXPECT_FLOAT_EQ(out[v4 + 0], 2.0f * 16.0f / 640.0f - 1.0f);
	EXPECT_FLOAT_EQ(out[v4 + 1], 1.0f - 2.0f * 24.0f / 480.0f);
}

TEST(OsdGlVertices, ColorChannelsAreNormalised)
{
	std::vector<float> out = {};

	ASSERT_TRUE(
	        OSD::AppendRectVertices(out, {0, 0, 1, 1}, {255, 0, 128, 96}, 640, 480));

	EXPECT_FLOAT_EQ(out[2], 1.0f);
	EXPECT_FLOAT_EQ(out[3], 0.0f);
	EXPECT_FLOAT_EQ(out[4], 128.0f / 255.0f);
	EXPECT_FLOAT_EQ(out[5], 96.0f / 255.0f);

	// The color repeats identically on all six vertices.
	for (int v = 1; v < 6; ++v) {
		for (int c = 2; c < 6; ++c) {
			EXPECT_FLOAT_EQ(out[v * 6 + c], out[c]);
		}
	}
}

TEST(OsdGlVertices, RejectsDegenerateInput)
{
	std::vector<float> out = {};

	EXPECT_FALSE(OSD::AppendRectVertices(
	        out, {0, 0, 0, 16}, {255, 255, 255, 255}, 640, 480));
	EXPECT_FALSE(OSD::AppendRectVertices(
	        out, {0, 0, 8, -1}, {255, 255, 255, 255}, 640, 480));
	EXPECT_FALSE(OSD::AppendRectVertices(
	        out, {0, 0, 8, 16}, {255, 255, 255, 255}, 0, 480));
	EXPECT_FALSE(OSD::AppendRectVertices(
	        out, {0, 0, 8, 16}, {255, 255, 255, 255}, 640, 0));

	EXPECT_TRUE(out.empty());
}

#endif // C_OPENGL

} // namespace
