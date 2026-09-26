// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "truetype_output.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "augra/log.h"
#include "dos/dos.h"
#include "dosbox.h"
#include "gui/common.h"
#include "gui/render/render.h"
#include "gui/truetype_character_block.h"
#include "gui/truetype_freetype.h"
#include "hardware/pic.h"
#include "hardware/video/reelmagic/reelmagic.h"
#include "hardware/video/vga.h"
#include "ints/int10.h"
#include "misc/cross.h"
#include "misc/logging.h"
#include "misc/std_filesystem.h"
#include "misc/support.h"
#include "misc/unicode.h"
#include "utils/checks.h"
#include "utils/fs_utils.h"
#include "utils/math_utils.h"
#include "utils/mem_unaligned.h"
#include "utils/restrict.h"

CHECK_NARROWING();

// #define DEBUG_TTF_NO_CALLIBRATION
// #define DEBUG_TTF_NO_BORDER_SHARPENING
// #define DEBUG_TTF_NO_INVERSE_RENDERING
// #define DEBUG_TTF_NO_ASPECT_CORRECTION

// #define DEBUG_TTF_NO_SCREEN_CACHE

// Default font file (a 4:3 aspect-corrected Flexi IBM VGA font by VileR)
static const std::string DefaultFont = "Flexi_IBM_VGA_True.ttf";
// Console font resource subdirectory
static const std::string ResourceDir = "fonts-console";

// We do not want to use the TTF renderer if screen size (in charracter
// blocks) is above some sane level
constexpr uint32_t MaxSupportedBlocksHorizontal = 250;
constexpr uint32_t MaxSupportedBlocksVertical   = 100;

// Aligning some memory structures in memory can increase SIMD code performance
constexpr uint8_t SimdAlignment = 32; // value for AVX

// ***************************************************************************
// TTF module state
// ***************************************************************************

namespace OptionAspect {

constexpr auto Original   = "original";
constexpr auto Wide       = "wide";
constexpr auto WideSticky = "wide-sticky";
constexpr auto Font       = "font";

} // namespace OptionAspect

enum class AspectMode {
	Original,
	Wide,
	WideSticky,
	Font,
};

// If the module and a FreeType library is initialized and functional
static bool is_initialized = false;

// If the TTF screen renderer was enabled in the configuration
static bool is_ttf_enabled = false;
// Last font file read from the configuration
static std::string screen_font_file = {};
// Character block aspect ratio calculation mode
static AspectMode aspect_mode = {};

// If false, it was detected that the guest software modified the screen font
static bool is_screen_font_unaltered = true;

// If false, the screen size (in blocks) is wild and the TTF renderer should
// back off
static bool is_screen_size_sane = true;

// ***************************************************************************
// Rendering engine contants, tables, and helper functions
// ***************************************************************************

enum class Category {
	// Character not supported by the current font
	Unsupported,
	// Letter, number, punctuation symbol, etc.
	Regular,
	// A space character
	Space,
	// GUI drawing shape or symbol
	Symbol,
	// GUI drawing shape - up down arrow
	SymbolUpDownArrow,
	// Table or box drawing character
	Drawing,
	// Shaded full blocks
	Shade,
	// Symbols for drawing integrals
	Integral,
	// Ligature to be rendered from two 'Category::Regular' characters
	Ligature,
	// Ligature as above - characters used to render should not overlap
	LigatureNoOverlap,
	// Ukrainian hryvnia currency sign
	Hryvnia,
	// Double tilde sign
	DoubleTilde,

	// TODO: If glyph composition engine is implemented, consider rendering
	// U+F20D (COMMON LATIN CAPITAL LETTER D WITH HOOK AND TAIL) as U+0257
	// with U+0335
};

// List of code points from 'Box Drawing' and 'Block Elements' Unicode blocks
// which should touch all the borders - to be used for renderer callibration,
// ordered in the order of preferrence
static const std::vector<char32_t> CallibrationCodePointsDrawing = {
        // Typical drawing characters available in code page 437
        0x2588, // FULL BLOCK
        0x253c, // BOX DRAWINGS LIGHT VERTICAL AND HORIZONTAL
        0x256c, // BOX DRAWINGS DOUBLE VERTICAL AND HORIZONTAL
        0x256b, // BOX DRAWINGS VERTICAL DOUBLE AND HORIZONTAL SINGLE
        0x256a, // BOX DRAWINGS VERTICAL SINGLE AND HORIZONTAL DOUBLE
        // Other characters - 'Block Elements'
        0x259a, // QUADRANT UPPER LEFT AND LOWER RIGHT
        0x259e, // QUADRANT UPPER RIGHT AND LOWER LEFT
        0x2599, // QUADRANT UPPER LEFT AND LOWER LEFT AND LOWER RIGHT
        0x259b, // QUADRANT UPPER LEFT AND UPPER RIGHT AND LOWER LEFT
        0x259c, // QUADRANT UPPER LEFT AND UPPER RIGHT AND LOWER RIGHT
        0x259f, // QUADRANT UPPER RIGHT AND LOWER LEFT AND LOWER RIGHT
        // Other characters - 'Box Drawing'
        0x253d, // BOX DRAWINGS LEFT HEAVY AND RIGHT VERTICAL LIGHT
        0x253e, // BOX DRAWINGS RIGHT HEAVY AND LEFT VERTICAL LIGHT
        0x253f, // BOX DRAWINGS VERTICAL LIGHT AND HORIZONTAL LIGHT
        0x2540, // BOX DRAWINGS UP HEAVY AND DOWN HORIZONTAL LIGHT
        0x2541, // BOX DRAWINGS DOWN HEAVY AND UP HORIZONTAL LIGHT
        0x2542, // BOX DRAWINGS VERTICAL HEAVY AND HORIZONTAL LIGHT
        0x2543, // BOX DRAWINGS LEFT UP HEAVY AND RIGHT DOWN LIGHT
        0x2544, // BOX DRAWINGS RIGHT UP HEAVY AND LEFT DOWN LIGHT
        0x2545, // BOX DRAWINGS LEFT DOWN HEAVY AND RIGHT UP LIGHT
        0x2546, // BOX DRAWINGS RIGHT DOWN HEAVY AND LEFT UP LIGHT
        0x2547, // BOX DRAWINGS DOWN LIGHT AND UP HORIZONTAL HEAVY
        0x2548, // BOX DRAWINGS UP LIGHT AND DOWN HORIZONTAL HEAVY
        0x2549, // BOX DRAWINGS RIGHT LIGHT AND LEFT VERTICAL HEAVY
        0x254a, // BOX DRAWINGS LEFT LIGHT AND RIGHT VERTICAL HEAVY
        0x254b, // BOX DRAWINGS HEAVY VERTICAL AND HORIZONTAL
        // Last resort characters - 'Box Drawing'
        0x2571, // BOX DRAWINGS LIGHT DIAGONAL UPPER RIGHT TO LOWER LEFT
        0x2572, // BOX DRAWINGS LIGHT DIAGONAL UPPER LEFT TO LOWER RIGHT
        0x2573, // BOX DRAWINGS LIGHT DIAGONAL CROSS
};

static const std::vector<char32_t> CallibrationCodePointsShade = {
        0x2593, // DARK SHADE
        0x2592, // MEDIUM SHADE
        0x2591, // LIGHT SHADE
};

static const std::vector<char32_t> CallibrationCodePointsLigature = {
        0x006d, // LATIN SMALL LETTER M
        0x004d, // LATIN CAPITAL LETTER M
        0x0077, // LATIN SMALL LETTER W
        0x0057, // LATIN CAPITAL LETTER W
        0x00e6, // LATIN SMALL LIGATURE AE
        0x00c6, // LATIN CAPITAL LIGATURE AE
        0x0152, // LATIN CAPITAL LIGATURE OE
        0x0153, // LATIN SMALL LIGATURE OE
};

static const std::vector<char32_t> CallibrationCodePointsAspectRatio = {
        0x25cb, // WHITE CIRCLE
        0x2022, // BULLET
        0x25a0, // BLACK SQUARE
};

// Certain code points needing special treatment
constexpr char32_t CallibrationCodePointIntegralTop    = 0x2321;
constexpr char32_t CallibrationCodePointIntegralBottom = 0x2320;
constexpr char32_t CallibrationCodePointUpDownArrow    = 0x2195;

// All the code points which we can render as a space
static const std::set<char32_t> SpaceCodePoints = {
        0x0000, // NULL
        0x0020, // SPACE
        0x00a0, // NO-BREAK SPACE
        0x1680, // OGHAM SPACE MARK
        0x2000, // EN QUAD
        0x2001, // EM QUAD
        0x2002, // EN SPACE
        0x2003, // EM SPACE
        0x2004, // THREE-PER-EM SPACE
        0x2005, // FOUR-PER-EM SPACE
        0x2006, // SIX-PER-EM SPACE
        0x2007, // FIGURE SPACE
        0x2008, // PUNCTUATION SPACE
        0x2009, // THIN SPACE
        0x200a, // HAIR SPACE
        0x202f, // NARROW NO-BREAK SPACE
        0x205f, // MEDIUM MATHEMATICAL SPACE
        0x3000, // IDEOGRAPGHIC SPACE
};

// Code points which tend to look better (especially when facing other drawing
// character) if they are rendered as inverse of the other code point
static const std::unordered_map<char32_t, char32_t> RenderAsInverse = {
#ifndef DEBUG_TTF_NO_INVERSE_RENDERING
        {0x25d8, 0x2022},
        {0x25d9, 0x25cb},
// Another possible pair: { 0x2593, 0x2591 }
#endif
};

// In some cases, when the font does not contain a glyph for a ligature,
// we can work this around by rendering two letters in a clever way;
// results might vary between fonts, but at least this allows us to
// support code pages containing the given ligature
static const std::unordered_map<char32_t, std::pair<char32_t, char32_t>> SupportedLigatures = {
        // Standard Unicode ligatures
        {0x00e6, {0x0061, 0x0065}}, // LATIN SMALL LIGATURE AE
        {0x0132, {0x0049, 0x004a}}, // LATIN CAPITAL LIGATURE IJ
        {0x0133, {0x0069, 0x006a}}, // LATIN SMALL LIGATURE IJ
        {0x0152, {0x004f, 0x0045}}, // LATIN CAPITAL LIGATURE OE
        {0x0153, {0x006e, 0x0065}}, // LATIN SMALL LIGATURE OE
        {0x04a4, {0x041d, 0x0413}}, // CYRILLIC CAPITAL LIGATURE EN GHE
        {0x04a5, {0x043d, 0x0433}}, // CYRILLIC SMALL LIGATURE EN GHE
        {0x04d5, {0x0430, 0x0435}}, // CYRILLIC SMALL LIGATURE A IE
        // DOSBox private ligatures
        {0xedb0, {0x007a, 0x0142}}, // PRIVATE DOSBOX PLN SYMBOL
        {0xedb2, {0x0423, 0x041e}}, // PRIVATE DOSBOX CYRILLIC CAPITAL LIGATURE  UO
        {0xedb3, {0x0443, 0x043e}}, // PRIVATE DOSBOX CYRILLIC SMALL LIGATURE UO

        // Current ligature fallback support code can only render in a sane way
        // some of the ligatures. It can't work for the following ones:
        // - U+00C6 - LATIN CAPITAL LIGATURE AE
        // - U+04B4 - CYRILLIC CAPITAL LIGATURE TE TSE
        // - U+04B5 - CYRILLIC SMALL LIGATURE TE TSE
        // - U+04D4 - CYRILLIC CAPITAL LIGATURE A IE
        // - U+0587 - ARMENIAN SMALL LIGATURE ECH YIWN
        // - U+05F0 - HEBREW LIGATURE YIDDISH DOUBLE VAV
        // - U+05F1 - HEBREW LIGATURE YIDDISH VAV YOD
        // - U+05F2 - HEBREW LIGATURE YIDDISH DOUBLE YOD
};

// With most ligatures the letters overlap each other a little - there are some
// exceptions, this is the list
static const std::set<char32_t> NoOverlapLigatures = {
        0xedb0 // PRIVATE DOSBOX PLN SYMBOL
};

static Category get_default_category(const char32_t code_point,
                                     const uint8_t dos_code_point = 0)
{
	if (SpaceCodePoints.contains(code_point)) {
		return Category::Space;
	}

#ifdef DEBUG_TTF_NO_POSTPROCESSING
	return Category::Regular;
#endif

	if (dos_code_point < ' ') {

		// U+2195 - UP DOWN ARROW
		// U+21A8 - UP DOWN ARROW WITH BASE
		if (code_point == 0x2195 || code_point == 0x21a8) {
			return Category::SymbolUpDownArrow;
		}

		// U+2022 - BULLET
		if (code_point == 0x2022) {
			return Category::Symbol;
		}

		// 'Arrows' Unicode block
		if (code_point >= 0x2190 && code_point <= 0x21ff) {
			return Category::Symbol;
		}
	}

	// Possible tweak (results might vary, depending on the font) - assign
	// non-GUI symbols to the Category::Regular, these might include: U+2640
	// - FEMALE SIGN U+2642 - MALE SIGN U+266A - EIGHTH NOTE U+266B - BEAMED
	// EIGHTH NOTES and all the other musical notes, U+2669 - U+266F

	// U+2591 - LIGHT SHADE
	// U+2592 - MEDIUM SHADE
	// U+2593 - DARK SHADE
	if (code_point >= 0x2591 && code_point <= 0x2593) {
		return Category::Shade;
	}

	// U+2320 - TOP HALF INTEGRAL
	// U+2321 - BOTTOM HALF INTEGRAL
	if (code_point == 0x2320 || code_point == 0x2321) {
		return Category::Integral;
	}

	// 'Box Drawing' and 'Block Elements' Unicode blocks
	if (code_point >= 0x2500 && code_point <= 0x259f) {
		return Category::Drawing;
	}

	// 'Geometric Shapes' and 'Miscellaneous Symbols' Unicode blocks
	if (code_point >= 0x25a0 && code_point <= 0x26ff) {
		return Category::Symbol;
	}

	return Category::Regular;
}

// Check if the code point glyph requires aspect ratio correction after
// scaling to look right
static bool needs_aspect_ratio_correction(const char32_t code_point)
{
#ifdef DEBUG_TTF_NO_ASPECT_CORRECTION
	return false;
#endif

	// U+2669 - U+266F - musical notes
	if (code_point >= 0x2669 && code_point <= 0x266f) {
		return false;
	}

	// U+2022 - BULLET
	if (code_point == 0x2022) {
		return true;
	}

	// 'Geometric Shapes' and 'Miscellaneous Symbols' Unicode blocks
	if (code_point >= 0x25a0 && code_point <= 0x26ff) {
		return true;
	}

	return false;
}

// Check if the code point glyph requires sharpening of all the borders to look
// right when placed next to some other glyphs
static bool needs_sharpening_all_borders(const char32_t code_point)
{
#ifdef DEBUG_TTF_NO_BORDER_SHARPENING
	return false;
#endif
	if (get_default_category(code_point) != Category::Drawing) {
		return false;
	}

	if (code_point >= 0x2504 && code_point <= 0x250b) {
		return false;
	}

	if (code_point >= 0x254c && code_point <= 0x254f) {
		return false;
	}

	if (code_point >= 0x2571 && code_point <= 0x2573) {
		return false;
	}

	if (code_point >= 0x2591 && code_point <= 0x2593) {
		return false;
	}

	return true;
}

// Like above, but sharpening should be limited to the top border
static bool needs_sharpening_only_top(const char32_t code_point)
{
#ifdef DEBUG_TTF_NO_BORDER_SHARPENING
	return false;
#endif
	return (code_point == 0x2321);
}

// Like above, but sharpening should be limited to the bottom border
static bool needs_sharpening_only_bottom(const char32_t code_point)
{
#ifdef DEBUG_TTF_NO_BORDER_SHARPENING
	return false;
#endif
	return (code_point == 0x2320);
}

// ***************************************************************************
// Rendering engine single block support: truetype_character_block.h
// ***************************************************************************

using TrueType::BytesPerPixel;
using TrueType::CharacterBlock;

// ***************************************************************************
// Rendering engine font handling
// ***************************************************************************

struct RenderRecipe {
	FT_UInt glyph_index     = 0;
	Category glyph_category = {};

	std::optional<FT_UInt> glyph_index_secondary = 0;

	bool invert = false;

	bool sharpen_all_borders = false;
	bool sharpen_only_top    = false;
	bool sharpen_only_bottom = false;

	bool needs_aspect_ratio_correction = false;
};

class FontWrap {
public:
	bool Load(const std_fs::path& file_path);
	void Unload();

	bool IsLoaded() const
	{
		return is_loaded;
	}
	std_fs::path LoadedFilePath() const
	{
		return loaded_file_path;
	}

	// Check if code page is compatible with the current code page
	bool IsCompatible();

	float GetReportedAspectRatio() const;

	void PreRenderBlocks(const uint32_t width_px, const uint32_t height_px);

	void RenderInGrey(uint8_t* const destination, const uint8_t character,
	                  const uint32_t line);

	~FontWrap()
	{
		Unload();
	}

private:
	struct CallibrationData {
		uint32_t block_width_px  = 0;
		uint32_t block_height_px = 0;

		// Shift the rendering by given number of pixels
		float delta_horizontal = 0.0f;
		float delta_vertical   = 0.0f;

		// Stretch to use given number of extra pixels
		float stretch_horizontal = 0.0f;
		float stretch_vertical   = 0.0f;
	};

	struct CallibrationIndexes {
		FT_UInt drawing         = 0;
		FT_UInt shade           = 0;
		FT_UInt integral_top    = 0;
		FT_UInt integral_bottom = 0;
		FT_UInt up_down_arrow   = 0;
		FT_UInt aspect_ratio    = 0;
	};

	RenderRecipe CreateRecipe(const char32_t code_point,
	                          const uint8_t dos_code_point,
	                          std::set<char32_t>& missing_glyphs);

	RenderRecipe CreateRecipeLigature(const char32_t code_point,
	                                  std::set<char32_t>& missing_glyphs);

	RenderRecipe CreateRecipeHryvnia(const char32_t code_point,
	                                 std::set<char32_t>& missing_glyphs);

	RenderRecipe CreateRecipeDoubleTilde(std::set<char32_t>& missing_glyphs);

	CharacterBlock GetBlock(const FT_Bitmap& bitmap,
	                        const FT_Int bitmap_left, const FT_Int bitmap_top,
	                        const uint32_t size_horizontal_px,
	                        const uint32_t size_vertical_px) const;

	CharacterBlock RenderBlockGeneric(const FT_UInt glyph_index,
	                                  const CallibrationData& callibration,
	                                  const FT_Render_Mode render_mode);

	CharacterBlock RenderBlockPreserveAspectRatio(
	        const FT_UInt glyph_index, const CallibrationData& callibration,
	        const FT_Render_Mode render_mode, const bool center = false);

	CharacterBlock RenderBlockSymbol(const FT_UInt glyph_index,
	                                 const bool preserve_aspect,
	                                 const bool is_up_down_arrow);

	CharacterBlock RenderBlockLigature(const FT_UInt glyph_index_1,
	                                   const FT_UInt glyph_index_2,
	                                   const bool should_overlap = true);

	CharacterBlock RenderBlockShade(const FT_UInt glyph_index);

	CharacterBlock RenderBlockHryvnia(const FT_UInt glyph_index_1,
	                                  const FT_UInt glyph_index_2);

	CharacterBlock RenderBlockDoubleTilde(const FT_UInt glyph_index_1,
	                                      const FT_UInt glyph_index_2);

	CharacterBlock RenderBlock(const RenderRecipe& recipe);

	FT_BBox GetBoundingBox(const FT_UInt glyph_index);
	FT_BBox GetFontBoundingBox();

	CallibrationData TweakTouchLeft(const FT_UInt glyph_index,
	                                const FontWrap::CallibrationData& base,
	                                const uint8_t max_steps);

	CallibrationData TweakTouchRight(const FT_UInt glyph_index,
	                                 const FontWrap::CallibrationData& base,
	                                 const uint8_t max_steps);

	CallibrationData TweakTouchTop(const FT_UInt glyph_index,
	                               const FontWrap::CallibrationData& base,
	                               const uint8_t max_steps);

	CallibrationData TweakTouchBottom(const FT_UInt glyph_index,
	                                  const FontWrap::CallibrationData& base,
	                                  const uint8_t max_steps);

	CallibrationData TweakCenter(const FT_UInt glyph_index,
	                             const FontWrap::CallibrationData& base,
	                             const bool is_up_down_arrow = false);

	CallibrationData TweakGeneric(const Category existing, const Category fallback,
	                              const FT_UInt glyph_index);

	CallibrationData TweakIntegral(const Category existing,
	                               const Category fallback,
	                               const FT_UInt glyph_index_top,
	                               const FT_UInt glyph_index_bottom);

	void CallibrateRenderer();

	void ReportMissingGlyphs(const std::set<char32_t>& missing_glyphs) const;

	FT_Face face = {};

	static constexpr auto Identity       = 0x10000L;
	static constexpr auto PointsPerPixel = 64;

	static constexpr auto LigatureOverlap = 0.08f;

	// If the face was loaded succesfully
	bool is_loaded = false;

	// Path to the file loaded
	std_fs::path loaded_file_path = {};

	// Values from DOS taken when font compatibility was changed
	// for the last time
	uint16_t dos_code_page = 0;
	ScreenFontType dos_screen_font_type = ScreenFontType::Custom;

	// If the font is compatible with the current code page
	bool is_compatible = false;

	static constexpr float CallibrationStep = 0.5f;
	static constexpr uint8_t MaxStepDelta   = 5;
	static constexpr uint8_t MaxStepStretch = 5;

	FT_BBox bounding_box = {};

	float bounding_box_width  = 0.0f;
	float bounding_box_height = 0.0f;

	CallibrationIndexes callibration_indexes = {};
	std::vector<FT_UInt> callibration_indexes_ligature = {};

	// Glyph indexes relevant to the DOS code page
	std::array<RenderRecipe, UINT8_MAX + 1> recipes = {};

	// Pre-rendered font bitmaps
	uint16_t pre_render_code_page = 0;
	uint32_t pre_render_width_px  = 0;
	uint32_t pre_render_height_px = 0;
	std::vector<CharacterBlock> pre_rendered = {};

	std::unordered_map<Category, CallibrationData> callibration = {};

	// Target distance from the left/right block border when composing a
	// ligature from two glyphs
	float ligature_distance_left  = 0.0f;
	float ligature_distance_right = 0.0f;

	// Distance from the top/bottom block border of the up/down arrow character
	float up_down_arrow_distance_top    = 0.0f;
	float up_down_arrow_distance_bottom = 0.0f;

	// Font aspect ratio detected by analysing certain glyph rendered to the
	// target resolution; to be used for small drawing symbol corrections
	float detected_font_aspect_ratio = 0.0f;

	void ResetCallibration();
	void ApplyCallibration(const CallibrationData& callibration);
};

static FontWrap screen_font = {};

void FontWrap::Unload()
{
	if (is_loaded) {
		FreeType::DoneFace(face);

		is_loaded = false;
		loaded_file_path = "";

		dos_code_page = 0;
		dos_screen_font_type = ScreenFontType::Custom;

		is_compatible = false;

		callibration_indexes = CallibrationIndexes();
		callibration_indexes_ligature.clear();

		ligature_distance_left  = 0.0f;
		ligature_distance_right = 0.0f;

		up_down_arrow_distance_top    = 0.0f;
		up_down_arrow_distance_bottom = 0.0f;

		detected_font_aspect_ratio = 0.0f;

		pre_rendered.clear();
	}
}

bool FontWrap::Load(const std_fs::path& file_path)
{
	Unload();

	is_loaded = FreeType::NewFace(file_path, 0, &face);
	if (!is_loaded) {
		return false;
	}

	if (!(face->face_flags & FT_FACE_FLAG_FIXED_WIDTH)) {
		augra::log_warn("ttf",
		                "Font '%s' is not monospace, it cannot be used",
		                file_path.string().c_str());
		return false;
	}

	if (face->face_flags & FT_FACE_FLAG_TRICKY) {
		augra::log_warn("ttf",
		                "Font '%s' is considered tricky by FreeType, it cannot be used",
		                file_path.string().c_str());
		return false;
	}

	if (!(face->face_flags & FT_FACE_FLAG_SCALABLE)) {
		augra::log_warn("ttf",
		                "Font '%s' is a bitmap font, this is not supported",
		                file_path.string().c_str());
		return false;
	}

	for (const auto code_point : CallibrationCodePointsDrawing) {
		const auto glyph_index = FreeType::GetCharIndex(face, code_point);
		if (glyph_index != 0) {
			callibration_indexes.drawing = glyph_index;
			break;
		}
	}

	for (const auto code_point : CallibrationCodePointsShade) {
		const auto glyph_index = FreeType::GetCharIndex(face, code_point);
		if (glyph_index != 0) {
			callibration_indexes.shade = glyph_index;
			break;
		}
	}

	for (const auto code_point : CallibrationCodePointsAspectRatio) {
		const auto glyph_index = FreeType::GetCharIndex(face, code_point);
		if (glyph_index != 0) {
			callibration_indexes.aspect_ratio = glyph_index;
			break;
		}
	}

	for (const auto code_point : CallibrationCodePointsLigature) {
		const auto glyph_index = FreeType::GetCharIndex(face, code_point);
		if (glyph_index != 0) {
			callibration_indexes_ligature.push_back(glyph_index);
		}
	}

	callibration_indexes.integral_top =
	        FreeType::GetCharIndex(face, CallibrationCodePointIntegralTop);
	callibration_indexes.integral_bottom =
	        FreeType::GetCharIndex(face, CallibrationCodePointIntegralBottom);
	callibration_indexes.up_down_arrow =
	        FreeType::GetCharIndex(face, CallibrationCodePointUpDownArrow);

	loaded_file_path = file_path;
	return true;
}

RenderRecipe FontWrap::CreateRecipeLigature(const char32_t code_point,
                                            std::set<char32_t>& missing_glyphs)
{
	RenderRecipe recipe = {};

	recipe.glyph_category = Category::Unsupported;

	assert(SupportedLigatures.contains(code_point));

	// We have a fallback code capable of rendering this ligature;
	// check if the font contains the ingredients
	const auto code_point_1 = SupportedLigatures.at(code_point).first;
	const auto code_point_2 = SupportedLigatures.at(code_point).second;

	const auto index_1 = FreeType::GetCharIndex(face, code_point_1);
	const auto index_2 = FreeType::GetCharIndex(face, code_point_2);

	if (index_1 != 0 && index_2 != 0) {
		recipe.glyph_index           = index_1;
		recipe.glyph_index_secondary = index_2;

		if (NoOverlapLigatures.contains(code_point)) {
			recipe.glyph_category = Category::LigatureNoOverlap;
		} else {
			recipe.glyph_category = Category::Ligature;
		}

		// We can render this ligature with our fallback code
		return recipe;
	}

	// Characters needed by our ligature renderer are not available
	if (is_code_point_private(code_point)) {
		// These are private code points, do not report them;
		// report the missing fallback code points instead
		if (index_1 == 0) {
			missing_glyphs.insert(code_point_1);
		}
		if (index_2 == 0) {
			missing_glyphs.insert(code_point_2);
		}
	} else {
		missing_glyphs.insert(code_point);
	}

	// It's not possible to render this glyph
	return recipe;
}

RenderRecipe FontWrap::CreateRecipeHryvnia(const char32_t code_point,
                                           std::set<char32_t>& missing_glyphs)
{
	RenderRecipe recipe   = {};
	recipe.glyph_category = Category::Unsupported;

	// U+0053 - LATIN CAPITAL LETTER S
	constexpr uint32_t code_point_1 = 0x0053;
	// U+002D - HYPHEN-MINUS
	constexpr uint32_t code_point_2 = 0x002d;

	const auto index_1 = FreeType::GetCharIndex(face, code_point_1);
	const auto index_2 = FreeType::GetCharIndex(face, code_point_2);

	if (index_1 != 0 && index_2 != 0) {
		recipe.glyph_index           = index_1;
		recipe.glyph_index_secondary = index_2;

		recipe.glyph_category = Category::Hryvnia;

		// We can render this glyph with our fallback code
		return recipe;
	}

	// It's not possible to render this glyph
	missing_glyphs.insert(code_point);
	return recipe;
}

RenderRecipe FontWrap::CreateRecipeDoubleTilde(std::set<char32_t>& missing_glyphs)
{
	RenderRecipe recipe   = {};
	recipe.glyph_category = Category::Unsupported;

	// U+007E - TILDE
	constexpr uint32_t code_point_1 = 0x007e;
	// U+002D - HYPHEN-MINUS
	constexpr uint32_t code_point_2 = 0x002d;

	const auto index_1 = FreeType::GetCharIndex(face, code_point_1);
	// U+002D - HYPHEN-MINUS
	const auto index_2 = FreeType::GetCharIndex(face, code_point_2);

	if (index_1 != 0 && index_2 != 0) {
		recipe.glyph_index           = index_1;
		recipe.glyph_index_secondary = index_2;

		recipe.glyph_category = Category::DoubleTilde;

		// We can render this glyph with our fallback code
		return recipe;
	}

	// This is a DOSBox private code point, do not report it;
	// report the missing fallback code point instead
	if (index_1 == 0) {
		missing_glyphs.insert(code_point_1);
	}
	if (index_2 == 0) {
		missing_glyphs.insert(code_point_2);
	}

	// It's not possible to render this glyph
	return recipe;
}

RenderRecipe FontWrap::CreateRecipe(const char32_t code_point,
                                    const uint8_t dos_code_point,
                                    std::set<char32_t>& missing_glyphs)
{
	RenderRecipe recipe = {};

	recipe.glyph_category = get_default_category(code_point, dos_code_point);

	// For code points which should be rendered as space character (i.e.
	// U+00A0 - NO-BREAK SPACE) do not even bother checking the font
	if (recipe.glyph_category == Category::Space) {
		return recipe;
	}

	// Handle normal glyphs - not DOSBox specific, directly provided by the
	// font
	if (!is_code_point_dosbox_specific(code_point)) {
		recipe.glyph_index = FreeType::GetCharIndex(face, code_point);

		// Some characters should better be rendered as inverse of others
		if (RenderAsInverse.contains(code_point)) {
			const auto inverse_code_point = RenderAsInverse.at(code_point);
			const auto inverse_index =
			        FreeType::GetCharIndex(face, inverse_code_point);

			if (inverse_index != 0) {
				recipe.glyph_index = inverse_index;
				recipe.invert      = true;
			}
		}

		// Some characters intended for drawing should have
		// non-antialiased borders, as they can touch other drawing
		// elements to, for example, form a longer lines
		if (needs_sharpening_all_borders(code_point)) {
			recipe.sharpen_all_borders = true;
		} else if (needs_sharpening_only_top(code_point)) {
			recipe.sharpen_only_top = true;
		} else if (needs_sharpening_only_bottom(code_point)) {
			recipe.sharpen_only_bottom = true;
		}

		if (needs_aspect_ratio_correction(code_point)) {
			recipe.needs_aspect_ratio_correction = true;
		}

		// If we have a valid recipe, no further processing is needed
		if (recipe.glyph_index != 0) {
			return recipe;
		}
	}

	// Some ligatures not present in the font (also private code points) can
	// be rendered using our image processing code, by combining two other
	// glyphs
	if (SupportedLigatures.contains(code_point)) {
		return CreateRecipeLigature(code_point, missing_glyphs);
	}

	// We can render the hryvnia sign using letter 'S' and '-' signs
	if (code_point == 0x20b4) {
		return CreateRecipeHryvnia(code_point, missing_glyphs);
	}

	// We can render the double tilde using a normal tilde
	if (code_point == 0xedb1) {
		return CreateRecipeDoubleTilde(missing_glyphs);
	}

	// Glyph is not directly supported by the font
	recipe.glyph_category = Category::Unsupported;
	missing_glyphs.insert(code_point);

	return recipe;
}

void FontWrap::ReportMissingGlyphs(const std::set<char32_t>& missing_glyphs) const
{
	if (missing_glyphs.empty()) {
		return;
	}

	std::set<char32_t> unicode_official_glyphs = {};
	std::set<char32_t> private_known_glyphs    = {};
	std::set<char32_t> private_dosbox_glyphs   = {};
	std::set<char32_t> private_unknown_glyphs  = {};

	// Categorize missing glyphs
	for (const auto code_point : missing_glyphs) {
		if (is_code_point_dosbox_specific(code_point)) {
			private_dosbox_glyphs.insert(code_point);
		} else if (is_code_point_private_well_known(code_point)) {
			private_known_glyphs.insert(code_point);
		} else if (is_code_point_private(code_point)) {
			private_unknown_glyphs.insert(code_point);
			// We should not use any unknown private code points
			assert(false);
		} else {
			unicode_official_glyphs.insert(code_point);
		}
	}

	auto to_string = [&](const std::set<char32_t>& glyphs) {
		std::string message = {};
		std::string line    = {};

		constexpr uint32_t LineBreakAt = 100;

		for (const auto code_point : glyphs) {
			const auto code_point_str = format_str("U+%04X", code_point);
			if (line.size() + 3 + code_point_str.size() > LineBreakAt) {
				if (!message.empty()) {
					message += ",\n";
				}
				message += line;
				line.clear();
			}

			if (!line.empty()) {
				line += ", ";
			}
			line += code_point_str;
		}
		if (!line.empty()) {
			if (!message.empty()) {
				message += ",\n";
			}
			message += line;
		}

		return message;
	};

	augra::log_warn("ttf",
	                "Code page %d cannot be displayed using the current font due to missing glyphs",
	                dos_code_page);

	if (!unicode_official_glyphs.empty()) {
		augra::log_warn("ttf",
		                "Missing Unicode official code points:\n%s",
		                to_string(unicode_official_glyphs).c_str());
	}
	if (!private_known_glyphs.empty()) {
		augra::log_warn("ttf",
		                "Missing Unicode private (well-known) code points:\n%s",
		                to_string(private_known_glyphs).c_str());
	}
	if (!private_dosbox_glyphs.empty()) {
		augra::log_warn("ttf",
		                "Missing Unicode private (DOSBox-specific) code points:\n%s",
		                to_string(private_dosbox_glyphs).c_str());
	}
	if (!private_unknown_glyphs.empty()) {
		augra::log_warn("ttf",
		                "Missing Unicode (private) code points:\n%s",
		                to_string(private_unknown_glyphs).c_str());
	}
}

bool FontWrap::IsCompatible()
{
	if (!is_loaded) {
		return false;
	}

	// Check for the cached result
	if (dos_code_page != 0 && dos.loaded_codepage == dos_code_page &&
	    dos.screen_font_type == dos_screen_font_type) {
		return is_compatible;
	}

	// Re-check compatibility
	is_compatible = false;

	// Take a snapshot of important DOS settings
	dos_code_page        = dos.loaded_codepage;
	dos_screen_font_type = dos.screen_font_type;

	// We are only compatible with bundled CPI files or ROM fonts,
	// custom CPI files might contain just about anything and
	// we cannot handle that
	if (dos_screen_font_type != ScreenFontType::Rom &&
	    dos_screen_font_type != ScreenFontType::Bundled) {
		augra::log_warn("ttf",
		                "Only ROM fonts or bundled CPI files can be handled by the font engine");
		return false;
	}

	if (!is_code_page_supported(dos_code_page)) {
		augra::log_warn("ttf",
		                "Code page %d cannot be displayed by the current font engine",
		                dos_code_page);
		return false;
	}

	// Search for the glyph indexes relevant to the DOS code page
	std::set<char32_t> missing_glyphs = {};
	for (size_t idx = 0; idx < recipes.size(); ++idx) {
		const auto code_points = dos_to_unicode(format_str("%c", idx),
		                                        DosStringConvertMode::ScreenCodesOnly);
		// We can't display characters which use combining marks,
		// FreeType alone can't render such graphemes, we would need a
		// text shaping engine (TODO: write a simple one?).
		char32_t code_point = {};
		if (code_points.empty()) {
			// Code page deos not contain this code point - use space
			code_point = ' ';
		} else if (code_points.size() == 2 &&
		           SpaceCodePoints.contains(code_points[0])) {
			// Space + combining mark
			code_point = code_points[1];
		} else if (code_points.size() > 1) {
			augra::log_warn("ttf",
			                "Code page %d uses combining marks, "
			                "this is not supported by the current font engine",
			                dos_code_page);
			return false;
		} else {
			code_point = code_points[0];
		}

		recipes[idx] = CreateRecipe(code_point,
		                            static_cast<uint8_t>(idx),
		                            missing_glyphs);
	}

	if (missing_glyphs.empty()) {
		is_compatible = true;
		return true;
	}

	// Report the missing glyphs in the log output
	ReportMissingGlyphs(missing_glyphs);
	return false;
}

float FontWrap::GetReportedAspectRatio() const
{
	return static_cast<float>(face->max_advance_width) /
	       static_cast<float>(face->max_advance_height);
}

CharacterBlock FontWrap::GetBlock(const FT_Bitmap& bitmap,
                                  const FT_Int bitmap_left, const FT_Int bitmap_top,
                                  const uint32_t size_horizontal_px,
                                  const uint32_t size_vertical_px) const
{
	CharacterBlock block(size_horizontal_px, size_vertical_px);

	// Code based on https://freetype.org/freetype2/docs/tutorial/example1.c

	const int x_min = bitmap_left;
	const int y_min = size_vertical_px - bitmap_top;
	const int x_max = x_min + bitmap.width;
	const int y_max = y_min + bitmap.rows;

	auto get_pixel_grey = [&](const int p, const int q) -> uint8_t {
		const auto index = q * bitmap.pitch + p;

		return bitmap.buffer[index];
	};

	auto get_pixel_mono = [&](const int p, const int q) -> uint8_t {
		const auto index = q * bitmap.pitch + p / 8;

		const auto bit_num  = 7 - (p % 8);
		const auto bit_mask = (1 << bit_num);

		return (bitmap.buffer[index] & bit_mask) ? 0xff : 0;
	};

	int p = 0;
	for (int x = x_min; x < x_max; ++x, ++p) {
		if (x < 0 || x >= clamp_to_int32(block.GetWidth())) {
			continue;
		}
		int q = 0;
		for (int y = y_min; y < y_max; ++y, ++q) {
			if (y < 0 || y >= clamp_to_int32(block.GetHeight())) {
				continue;
			}

			uint8_t pixel_value = 0;
			switch (bitmap.pixel_mode) {
			case FT_PIXEL_MODE_GRAY:
				pixel_value = get_pixel_grey(p, q);
				break;
			case FT_PIXEL_MODE_MONO:
				pixel_value = get_pixel_mono(p, q);
				break;
			default: assert(false); break;
			}

			block.SetPixel(static_cast<uint16_t>(x),
			               static_cast<uint16_t>(y),
			               pixel_value);
		}
	}

	return block;
}

CharacterBlock FontWrap::RenderBlockGeneric(const FT_UInt glyph_index,
                                            const CallibrationData& callibration,
                                            const FT_Render_Mode render_mode)
{
	ApplyCallibration(callibration);

	FreeType::LoadGlyph(face, glyph_index, 0);
	FreeType::RenderGlyph(face->glyph, render_mode);

	return GetBlock(face->glyph->bitmap,
	                face->glyph->bitmap_left,
	                face->glyph->bitmap_top,
	                callibration.block_width_px,
	                callibration.block_height_px);
}

CharacterBlock FontWrap::RenderBlockPreserveAspectRatio(
        const FT_UInt glyph_index, const CallibrationData& callibration,
        const FT_Render_Mode render_mode, const bool center)
{
	auto callibration_tweaked = callibration;

	const auto block = RenderBlockGeneric(glyph_index, callibration, render_mode);

	// Preserve the font aspect ratio - if we have it detected
	if (detected_font_aspect_ratio > 0.0f) {
		const auto coefficient = detected_font_aspect_ratio - 1.0f;
		const auto diff_pixels = coefficient *
		                         static_cast<float>(block.GetHeight());

		callibration_tweaked.stretch_vertical += diff_pixels;

		const auto distance_to_middle = callibration_tweaked.delta_vertical +
		                                block.GetDistanceBottom() +
		                                block.GetContentHeight() / 2;

		callibration_tweaked.delta_vertical -= coefficient *
		                                       distance_to_middle / 2;
	}

	if (center) {
		// Due to rounding errors and imprecisions the aspect ratio
		// correction above can sometimes move the previously centered
		// glyph slightly off-center

		callibration_tweaked = TweakCenter(glyph_index, callibration_tweaked);
	}

	return RenderBlockGeneric(glyph_index, callibration_tweaked, render_mode);
}

CharacterBlock FontWrap::RenderBlockSymbol(const FT_UInt glyph_index,
                                           const bool preserve_aspect,
                                           const bool is_up_down_arrow)
{
	constexpr auto RenderMode = FT_RENDER_MODE_NORMAL;

	auto callibration_tweaked = TweakCenter(glyph_index,
	                                        callibration[Category::Symbol],
	                                        is_up_down_arrow);

	if (preserve_aspect) {
		return RenderBlockPreserveAspectRatio(glyph_index,
		                                      callibration_tweaked,
		                                      RenderMode,
		                                      !is_up_down_arrow);
	} else {
		return RenderBlockGeneric(glyph_index, callibration_tweaked, RenderMode);
	}
}

CharacterBlock FontWrap::RenderBlockLigature(const FT_UInt glyph_index_1,
                                             const FT_UInt glyph_index_2,
                                             const bool should_overlap)
{
	constexpr auto Flags     = FT_RENDER_MODE_NORMAL;
	constexpr float MinWidth = 1.0f;

	const auto block_width = static_cast<float>(pre_render_width_px);

	const auto overlap = should_overlap ? block_width * LigatureOverlap : 0.0f;

	const auto& callibration_base = callibration[Category::Regular];

	auto callibration_1 = callibration_base;
	auto callibration_2 = callibration_base;

	const auto block_1 = RenderBlockGeneric(glyph_index_1, callibration_base, Flags);
	const auto block_2 = RenderBlockGeneric(glyph_index_2, callibration_base, Flags);

	const auto distance_1_left  = block_1.GetDistanceLeft();
	const auto distance_1_right = block_1.GetDistanceRight();
	const auto distance_2_left  = block_2.GetDistanceLeft();
	const auto distance_2_right = block_2.GetDistanceRight();

	auto width_1 = block_width - distance_1_left - distance_1_right;
	auto width_2 = block_width - distance_2_left - distance_2_right;

	width_1 = std::max(MinWidth, width_1);
	width_2 = std::max(MinWidth, width_2);

	auto width_allowed = block_width - ligature_distance_left -
	                     ligature_distance_right;
	width_allowed = std::min(width_allowed, width_1 + width_2);

	auto width_allowed_1 = width_allowed * width_1 / (width_1 + width_2) + overlap;
	auto width_allowed_2 = width_allowed * width_2 / (width_1 + width_2) + overlap;

	width_allowed_1 = std::min(width_allowed_1, width_1);
	width_allowed_2 = std::min(width_allowed_2, width_2);

	callibration_1.stretch_horizontal -= width_1 - width_allowed_1;
	callibration_2.stretch_horizontal -= width_2 - width_allowed_2;

	callibration_1.delta_horizontal -= distance_1_left - ligature_distance_left;
	callibration_2.delta_horizontal -= distance_2_left - ligature_distance_left +
	                                   overlap * 2 - width_allowed_1;

	auto block = RenderBlockGeneric(glyph_index_1, callibration_1, Flags);
	block.Blend(RenderBlockGeneric(glyph_index_2, callibration_2, Flags));

	return block;
}

CharacterBlock FontWrap::RenderBlockShade(const FT_UInt glyph_index)
{
	ApplyCallibration(callibration.at(Category::Shade));

	FreeType::LoadGlyph(face, glyph_index, 0);
	FreeType::RenderGlyph(face->glyph, FT_RENDER_MODE_NORMAL);

	auto bitmap_left = face->glyph->bitmap_left;
	auto bitmap_top  = face->glyph->bitmap_top;

	const auto glyph_bounding_box = GetBoundingBox(glyph_index);

	const auto glyph_bounding_box_width  = glyph_bounding_box.xMax -
	                                       glyph_bounding_box.xMin;
	const auto glyph_bounding_box_height = glyph_bounding_box.yMax -
	                                       glyph_bounding_box.yMin;

	FreeType::LoadGlyph(face, glyph_index, 0);

	FT_Outline grid = {};

	const auto num_points   = face->glyph->outline.n_points;
	const auto num_contours = face->glyph->outline.n_contours;

	FreeType::OutlineNew(num_points * 9, num_contours * 9, &grid);
	grid.flags = face->glyph->outline.flags;

	uint32_t start_point   = 0;
	uint32_t start_contour = 0;
	for (int x = 0; x < 3; x++) {
		for (int y = 0; y < 3; y++) {
			FT_Outline tmp = {};

			FreeType::OutlineNew(num_points, num_contours, &tmp);

			FreeType::OutlineTranslate(&tmp,
			                           x * glyph_bounding_box_width,
			                           y * glyph_bounding_box_height);

			for (uint32_t i = 0; i < num_points; ++i) {
				*(grid.points + start_point + i) = *(tmp.points + i);
				*(grid.tags + start_point + i)   = *(tmp.tags + i);
			}
			start_point += num_points;

			for (uint32_t i = 0; i < num_contours; ++i) {
				*(grid.contours + start_contour + i) =
					*(tmp.contours + i);
			}
			start_contour += num_contours;

			FreeType::OutlineDone(&tmp);
		}
	}

	FT_Bitmap bitmap = {};
	FreeType::BitmapInit(&bitmap);

	FT_Raster_Params params = {};
	params.target           = &bitmap;
	params.flags            = FT_RASTER_FLAG_AA;

	FreeType::OutlineRender(&grid, &params);

	const auto block = GetBlock(bitmap,
	                            bitmap_left,
	                            bitmap_top,
	                            callibration.at(Category::Shade).block_width_px,
	                            callibration.at(Category::Shade).block_height_px);

	FreeType::BitmapDone(&bitmap);
	FreeType::OutlineDone(&grid);

	return block;
}

CharacterBlock FontWrap::RenderBlockHryvnia(const FT_UInt glyph_index_1,
                                            const FT_UInt glyph_index_2)
{
	constexpr auto Flags = FT_RENDER_MODE_NORMAL;

	const auto block_width  = static_cast<float>(pre_render_width_px);
	const auto block_height = static_cast<float>(pre_render_height_px);

	const auto& callibration_base = callibration[Category::Regular];

	const auto block_1 = RenderBlockGeneric(glyph_index_1, callibration_base, Flags);
	const auto block_2 = RenderBlockGeneric(glyph_index_2, callibration_base, Flags);

	const auto distance_1_left   = block_1.GetDistanceLeft();
	const auto distance_1_right  = block_1.GetDistanceRight();
	const auto distance_1_top    = block_1.GetDistanceTop();
	const auto distance_1_bottom = block_1.GetDistanceBottom();

	const auto distance_2_left   = block_2.GetDistanceLeft();
	const auto distance_2_right  = block_2.GetDistanceRight();
	const auto distance_2_top    = block_2.GetDistanceTop();
	const auto distance_2_bottom = block_2.GetDistanceBottom();

	const auto glyph_index_3 = glyph_index_2;

	auto callibration_1 = callibration_base;
	auto callibration_2 = callibration_base;
	auto callibration_3 = callibration_base;

	// Shift the letter 'S' horizontally so that it is going to end at the
	// right position after being mirrored
	callibration_1.delta_horizontal += distance_1_right - distance_1_left;

	// Position the '-' sign precisely in the middle of the 'S' letter

	const auto width_1  = block_width - distance_1_left - distance_1_right;
	const auto width_2  = block_width - distance_2_left - distance_2_right;
	const auto height_1 = block_height - distance_1_top - distance_1_bottom;
	const auto height_2 = block_height - distance_2_top - distance_2_bottom;

	const auto center_1_horizontal = width_1 / 2 + distance_1_left;
	const auto center_2_horizontal = width_2 / 2 + distance_2_left;
	const auto center_1_vertical   = height_1 / 2 + distance_1_bottom;
	const auto center_2_vertical   = height_2 / 2 + distance_2_bottom;

	callibration_2.delta_horizontal += (center_1_horizontal - center_2_horizontal);
	callibration_2.delta_vertical   += (center_1_vertical - center_2_vertical);

	// Duplicate the '-' sign

	constexpr float DistanceCoefficient = 0.85f;

	callibration_3 = callibration_2;
	callibration_2.delta_vertical += height_2 * DistanceCoefficient;
	callibration_3.delta_vertical -= height_2 * DistanceCoefficient;

	// Compose the final glyph as reversed 'S' blended with two '-' signs
	auto block = RenderBlockGeneric(glyph_index_1, callibration_1, Flags);
	block.MirrorHorizontally();
	block.Blend(RenderBlockGeneric(glyph_index_2, callibration_2, Flags));
	block.Blend(RenderBlockGeneric(glyph_index_3, callibration_3, Flags));

	return block;
}

CharacterBlock FontWrap::RenderBlockDoubleTilde(const FT_UInt glyph_index_1,
                                                const FT_UInt glyph_index_2)
{
	constexpr auto Flags = FT_RENDER_MODE_NORMAL;

	const auto block_height = static_cast<float>(pre_render_height_px);

	const auto& callibration_base = callibration[Category::Regular];

	const auto block_1 = RenderBlockGeneric(glyph_index_1, callibration_base, Flags);
	const auto block_2 = RenderBlockGeneric(glyph_index_2, callibration_base, Flags);

	const auto distance_1_top    = block_1.GetDistanceTop();
	const auto distance_1_bottom = block_1.GetDistanceBottom();
	const auto distance_2_top    = block_2.GetDistanceTop();
	const auto distance_2_bottom = block_2.GetDistanceBottom();

	const auto glyph_index_3 = glyph_index_1;

	auto callibration_1 = callibration_base;
	auto callibration_3 = callibration_base;

	// Position the '~' sign at the height of the '-' sign

	const auto height_1 = block_height - distance_1_top - distance_1_bottom;
	const auto height_2 = block_height - distance_2_top - distance_2_bottom;

	const auto center_1_vertical = height_1 / 2 + distance_1_bottom;
	const auto center_2_vertical = height_2 / 2 + distance_2_bottom;

	callibration_1.delta_vertical += (center_2_vertical - center_1_vertical);

	// Duplicate the '~' sign

	constexpr float DistanceCoefficient = 0.65f;

	callibration_3 = callibration_1;
	callibration_1.delta_vertical += height_1 * DistanceCoefficient;
	callibration_3.delta_vertical -= height_1 * DistanceCoefficient;

	// Compose the final glyph as two '~' signs, one above the other
	auto block = RenderBlockGeneric(glyph_index_1, callibration_1, Flags);
	block.Blend(RenderBlockGeneric(glyph_index_3, callibration_3, Flags));

	return block;
}

CharacterBlock FontWrap::RenderBlock(const RenderRecipe& recipe)
{
	auto block = CharacterBlock(pre_render_width_px, pre_render_height_px);

	const auto category = recipe.glyph_category;

	const bool is_up_down_arrow = (category == Category::SymbolUpDownArrow);
	const bool is_overlapping   = (category == Category::Ligature);
	const bool preserve_aspect  = recipe.needs_aspect_ratio_correction;

	// Call appropriate base renderer
	switch (category) {
	case Category::Space: break;
	case Category::Regular:
	case Category::Drawing:
	case Category::Integral:
	case Category::Shade:
		if (preserve_aspect) {
			block = RenderBlockPreserveAspectRatio(recipe.glyph_index,
			                                       callibration.at(category),
			                                       FT_RENDER_MODE_NORMAL);
		} else {
			block = RenderBlockGeneric(recipe.glyph_index,
			                           callibration.at(category),
			                           FT_RENDER_MODE_NORMAL);
		}
		break;
	case Category::Symbol:
	case Category::SymbolUpDownArrow:
		block = RenderBlockSymbol(recipe.glyph_index,
		                          preserve_aspect,
		                          is_up_down_arrow);
		break;
	case Category::Ligature:
	case Category::LigatureNoOverlap:
		if (recipe.glyph_index_secondary) {
			block = RenderBlockLigature(recipe.glyph_index,
			                            *recipe.glyph_index_secondary,
			                            is_overlapping);
		} else {
			assert(false);
		}
		break;
	case Category::Hryvnia:
		if (recipe.glyph_index_secondary) {
			block = RenderBlockHryvnia(recipe.glyph_index,
			                           *recipe.glyph_index_secondary);
		} else {
			assert(false);
		}
		break;
	case Category::DoubleTilde:
		block = RenderBlockDoubleTilde(recipe.glyph_index,
		                               *recipe.glyph_index_secondary);
		break;
	default: assert(false); break;
	}

	// Postprocess the rendered block
	if (recipe.invert) {
		block.Invert();
	} else if (recipe.sharpen_all_borders) {
		block.SharpenAllBorders();
	} else if (recipe.sharpen_only_top) {
		block.SharpenOnlyTop();
	} else if (recipe.sharpen_only_bottom) {
		block.SharpenOnlyBottom();
	}

	return block;
}

FT_BBox FontWrap::GetBoundingBox(const FT_UInt glyph_index)
{
	FreeType::LoadGlyph(face, glyph_index, 0);
	FreeType::RenderGlyph(face->glyph, FT_RENDER_MODE_MONO);

	FT_BBox box    = {};
	FT_Glyph glyph = {};
	FreeType::GetGlyph(face->glyph, &glyph);

	// 'FT_Glyph_Get_CBox(glyph, FT_GLYPH_BBOX_UNSCALED, &box);' would be
	// faster, but less precise
	FreeType::OutlineGetBBox(&face->glyph->outline, &box);
	FreeType::DoneGlyph(glyph);

	return box;
}

FT_BBox FontWrap::GetFontBoundingBox()
{
	if (callibration_indexes.drawing != 0) {
		return GetBoundingBox(callibration_indexes.drawing);
	}

	auto max = [](const FT_BBox& box1, const FT_BBox& box2) {
		FT_BBox result = {};

		result.xMin = std::min(box1.xMin, box2.xMin);
		result.xMax = std::max(box1.xMax, box2.xMax);
		result.yMin = std::min(box1.yMin, box2.yMin);
		result.yMax = std::max(box1.yMax, box2.yMax);

		return result;
	};

	// We have no good glyph for callibration - so go through all the glyphs
	// in the current code page
	auto box = GetBoundingBox(0);
	for (size_t idx = 0; idx < recipes.size(); ++idx) {
		box = max(box, GetBoundingBox(recipes[idx].glyph_index));
	}

	return box;
}

void FontWrap::ApplyCallibration(const CallibrationData& callibration)
{
	const auto scale_width  = (static_cast<float>(callibration.block_width_px) +
	                           callibration.stretch_horizontal) / bounding_box_width;
	const auto scale_height = (static_cast<float>(callibration.block_height_px) +
	                           callibration.stretch_vertical) / bounding_box_height;

	FT_Matrix matrix = {};
	FT_Vector delta  = {};

	matrix.xx = std::lround(Identity * scale_width);
	matrix.yy = std::lround(Identity * scale_height);

	delta.x = -bounding_box.xMin + std::lround(callibration.delta_horizontal * PointsPerPixel);
	delta.y = -bounding_box.yMin + std::lround(callibration.delta_vertical * PointsPerPixel);

	FreeType::SetPixelSizes(face,
	                        callibration.block_width_px,
	                        callibration.block_height_px);
	FreeType::SetTransform(face, &matrix, &delta);
}

void FontWrap::ResetCallibration()
{
	FT_Matrix matrix = {};
	FT_Vector delta  = {};
	matrix.xx        = Identity;
	matrix.yy        = Identity;

	FreeType::SetPixelSizes(face, pre_render_width_px, pre_render_height_px);
	FreeType::SetTransform(face, &matrix, &delta);
}

FontWrap::CallibrationData FontWrap::TweakTouchLeft(const FT_UInt glyph_index,
                                                    const FontWrap::CallibrationData& base,
                                                    const uint8_t max_steps)
{
	constexpr auto Flags = FT_RENDER_MODE_MONO;

	auto result = base;

	result.delta_horizontal = 0.0f;

	const auto block = RenderBlockGeneric(glyph_index, result, Flags);
	if (block.IsTouchingLeft()) {
		return result;
	}

	auto candidate = result;
	for (uint8_t idx = 1; idx <= max_steps; ++idx) {
		candidate.delta_horizontal = -idx * CallibrationStep;
		if (RenderBlockGeneric(glyph_index, candidate, Flags).IsTouchingLeft()) {
			return candidate;
		}
	}

	return result;
}

FontWrap::CallibrationData FontWrap::TweakTouchRight(const FT_UInt glyph_index,
                                                     const FontWrap::CallibrationData& base,
                                                     const uint8_t max_steps)
{
	constexpr auto Flags = FT_RENDER_MODE_MONO;

	auto result = base;

	result.stretch_horizontal = 0.0f;

	const auto block = RenderBlockGeneric(glyph_index, result, Flags);
	if (block.IsTouchingRight()) {
		return result;
	}

	auto candidate = result;
	for (uint8_t idx = 1; idx <= max_steps; ++idx) {
		candidate.stretch_horizontal = idx * CallibrationStep;
		if (RenderBlockGeneric(glyph_index, candidate, Flags).IsTouchingRight()) {
			return candidate;
		}
	}

	return result;
}

FontWrap::CallibrationData FontWrap::TweakTouchTop(const FT_UInt glyph_index,
                                                   const FontWrap::CallibrationData& base,
                                                   const uint8_t max_steps)
{
	constexpr auto Flags = FT_RENDER_MODE_MONO;

	auto result = base;

	result.stretch_vertical = 0.0f;

	const auto block = RenderBlockGeneric(glyph_index, result, Flags);
	if (block.IsTouchingTop()) {
		return result;
	}

	auto candidate = result;
	for (uint8_t idx = 1; idx <= max_steps; ++idx) {
		candidate.stretch_vertical = idx * CallibrationStep;
		if (RenderBlockGeneric(glyph_index, candidate, Flags).IsTouchingTop()) {
			return candidate;
		}
	}

	return result;
}

FontWrap::CallibrationData FontWrap::TweakTouchBottom(
        const FT_UInt glyph_index, const FontWrap::CallibrationData& base,
        const uint8_t max_steps)
{
	constexpr auto Flags = FT_RENDER_MODE_MONO;

	auto result = base;

	result.stretch_vertical = 0.0f;

	const auto block = RenderBlockGeneric(glyph_index, result, Flags);
	if (block.IsTouchingBottom()) {
		return result;
	}

	auto candidate = result;
	for (uint8_t idx = 1; idx <= max_steps; ++idx) {
		candidate.stretch_vertical = -idx * CallibrationStep;
		if (RenderBlockGeneric(glyph_index, candidate, Flags).IsTouchingBottom()) {
			return candidate;
		}
	}

	return result;
}

FontWrap::CallibrationData FontWrap::TweakCenter(const FT_UInt glyph_index,
                                                 const FontWrap::CallibrationData& base,
                                                 const bool is_up_down_arrow)
{
	constexpr auto RenderMode = FT_RENDER_MODE_NORMAL;

	auto callibration_tweaked = base;

	const auto block = RenderBlockGeneric(glyph_index,
	                                      callibration_tweaked,
	                                      RenderMode);

	const auto distance_left  = block.GetDistanceLeft();
	const auto distance_rigth = block.GetDistanceRight();

	const auto distance_top = is_up_down_arrow ? up_down_arrow_distance_top
	                                           : block.GetDistanceTop();
	const auto distance_bottom = is_up_down_arrow
	                                   ? up_down_arrow_distance_bottom
	                                   : block.GetDistanceBottom();

	callibration_tweaked.delta_horizontal -= distance_left;
	callibration_tweaked.delta_horizontal += (distance_left + distance_rigth) / 2;

	callibration_tweaked.delta_vertical -= distance_bottom;
	callibration_tweaked.delta_vertical += (distance_top + distance_bottom) / 2;

	return callibration_tweaked;
}

FontWrap::CallibrationData FontWrap::TweakGeneric(const Category existing,
                                                  const Category fallback,
                                                  const FT_UInt glyph_index)
{
	if (glyph_index == 0) {
		return callibration[fallback];
	}

	auto result = callibration[existing];

	result = TweakTouchLeft(glyph_index, result, MaxStepDelta);
	result = TweakTouchBottom(glyph_index, result, MaxStepDelta);

	const auto max_stretch_horizontal = -iroundf(result.delta_horizontal * 2) +
	                                    MaxStepStretch;
	const auto max_stretch_vertical = -iroundf(result.delta_vertical * 2) +
	                                  MaxStepStretch;

	result = TweakTouchRight(glyph_index,
	                         result,
	                         clamp_to_uint8(max_stretch_horizontal));
	result = TweakTouchTop(glyph_index,
	                       result,
	                       clamp_to_uint8(max_stretch_vertical));

	return result;
}

FontWrap::CallibrationData FontWrap::TweakIntegral(const Category existing,
                                                   const Category fallback,
                                                   const FT_UInt glyph_index_top,
                                                   const FT_UInt glyph_index_bottom)
{
	if (glyph_index_top == 0 || glyph_index_bottom == 0) {
		return callibration[fallback];
	}

	auto result = callibration[existing];

	result = TweakTouchBottom(glyph_index_bottom, result, MaxStepDelta);

	const auto max_stretch_y = iroundf(result.delta_vertical * 2) + MaxStepStretch;

	result = TweakTouchTop(glyph_index_top, result, clamp_to_uint8(max_stretch_y));

	return result;
}

void FontWrap::CallibrateRenderer()
{
	ResetCallibration();

	bounding_box = GetFontBoundingBox();
	bounding_box_width  = static_cast<float>(bounding_box.xMax - bounding_box.xMin) /
	                      PointsPerPixel;
	bounding_box_height = static_cast<float>(bounding_box.yMax - bounding_box.yMin) /
	                      PointsPerPixel;

	assert(bounding_box_width > 0.0f);
	assert(bounding_box_height > 0.0f);

	callibration.clear();
	callibration[Category::Regular].block_width_px  = pre_render_width_px;
	callibration[Category::Regular].block_height_px = pre_render_height_px;

	callibration[Category::Drawing] = TweakGeneric(Category::Regular,
	                                               Category::Regular,
	                                               callibration_indexes.drawing);

	callibration[Category::Shade] = TweakGeneric(Category::Regular,
	                                             Category::Drawing,
	                                             callibration_indexes.shade);

	callibration[Category::Integral] =
	        TweakIntegral(Category::Drawing,
	                      Category::Drawing,
	                      callibration_indexes.integral_top,
	                      callibration_indexes.integral_bottom);

	callibration[Category::Symbol] = callibration[Category::Drawing];

	constexpr auto Flags = FT_RENDER_MODE_NORMAL;

	// Additional callibration of ligature rendering
	bool first = true;
	for (const auto glyph_index : callibration_indexes_ligature) {
		const auto block = RenderBlockGeneric(glyph_index,
		                                      callibration[Category::Regular],
		                                      Flags);

		if (first) {
			ligature_distance_left  = block.GetDistanceLeft();
			ligature_distance_right = block.GetDistanceRight();

			first = false;
			continue;
		}

		ligature_distance_left  = std::min(ligature_distance_left,
		                                   block.GetDistanceLeft());
		ligature_distance_right = std::min(ligature_distance_right,
		                                   block.GetDistanceRight());
	}

	// Additional callibration of up down arrows
	const auto block_arrow = RenderBlockGeneric(callibration_indexes.up_down_arrow,
	                                            callibration[Category::Symbol],
	                                            Flags);
	up_down_arrow_distance_top    = block_arrow.GetDistanceTop();
	up_down_arrow_distance_bottom = block_arrow.GetDistanceBottom();

	// Detect aspect ratio - skip if both dimensions are below certain
	// threshold
	const float Threshold = 4.0f;

	const auto block = RenderBlockGeneric(callibration_indexes.aspect_ratio,
	                                      callibration[Category::Symbol],
	                                      Flags);

	const auto content_width  = block.GetContentWidth();
	const auto content_height = block.GetContentHeight();

	if (content_width > Threshold && content_height > Threshold) {
		detected_font_aspect_ratio = block.GetContentWidth() /
		                             block.GetContentHeight();
	} else {
		detected_font_aspect_ratio = 0.0f;
	}
}

void FontWrap::PreRenderBlocks(const uint32_t width_px, const uint32_t height_px)
{
	if (!IsCompatible()) {
		return;
	}

	if (width_px == pre_render_width_px && height_px == pre_render_height_px &&
	    dos_code_page == pre_render_code_page && !pre_rendered.empty()) {
		// Current pre-render data is still valid
		return;
	}

	// We need to pre-render font for the new size
	pre_rendered.clear();
	pre_render_width_px  = width_px;
	pre_render_height_px = height_px;
	pre_render_code_page = dos_code_page;

	CallibrateRenderer();

	pre_rendered.reserve(UINT8_MAX + 1);
	for (uint16_t idx = 0; idx <= UINT8_MAX; ++idx) {
		pre_rendered.push_back(RenderBlock(recipes[idx]));
	}
}

void FontWrap::RenderInGrey(uint8_t* const destination, const uint8_t character,
                            const uint32_t line)
{
	assert(pre_rendered.size() == UINT8_MAX + 1);
	pre_rendered.at(character).RenderInGrey(destination,
	                                        line % pre_render_height_px);
}

// ***************************************************************************
// Screen related type definitions
// ***************************************************************************

// Using arrays wastes some memory, but is significantly faster
using ColorLookupTable = std::array<Rgb888, UINT8_MAX + 1>;
using RenderDataLine   = std::array<uint8_t, ScalerMaxWidthTtf * BytesPerPixel>;

// ***************************************************************************
// Cache, to skip unnecessary line rendering
// ***************************************************************************

#ifndef DEBUG_TTF_NO_SCREEN_CACHE

struct ScreenCache {

public:
	// Adapt the cache to new screen parameters
	void Configure(const uint32_t new_blocks_horizontal,
	               const uint32_t new_blocks_vertical,
	               const uint32_t new_block_width_px,
	               const uint32_t new_block_height_px);

	// Free all the dynamically allocated memory if TTF subsystem is no
	// longer in use
	void FreeMemory();

	void MarkAllDirty();
	void MarkRenderLineDirty(const uint32_t render_line);
	void MarkBlockLineDirty(const uint32_t block_line);

	// Lines the cache holds; drawing may never index past it (ada-a08x)
	uint32_t GetRenderHeight() const
	{
		return static_cast<uint32_t>(cache.size());
	}

	bool IsLineDirty(const uint32_t render_line,
	                 const bool hercules_underline) const;

	void UpdateLine(const uint8_t* vram_address,
	                const uint32_t render_line,
	                const bool hercules_underline);

	const RenderDataLine& GetRenderData(const uint32_t render_line,
	                                    bool& is_line_dirty);
	const RenderDataLine& GetRenderData(const uint32_t render_line,
	                                    const uint32_t cursor_block,
	                                    const Rgb888& cursor_color,
	                                    bool& is_line_dirty);

	void UpdateColors(const ColorLookupTable& new_colors_foreground,
	                  const ColorLookupTable& new_colors_background);
	void UpdateVideoMemory(const uint8_t* vram_address,
	                       const uint32_t render_line);

private:
	// Screen information
	uint32_t blocks_horizontal = 0;
	uint32_t blocks_vertical   = 0;
	uint32_t block_width_px    = 0;
	uint32_t block_height_px   = 0;
	uint32_t render_width_px   = 0;
	uint32_t render_height_px  = 0;

	// Cached emulation-side screen data
	ColorLookupTable cached_colors_foreground             = {};
	ColorLookupTable cached_colors_background             = {};
	std::vector<std::vector<uint8_t>> cached_video_memory = {};

	// Cache and metadata
	using CacheEntry = struct CacheEntry {
		// Rendered data
		alignas(SimdAlignment) RenderDataLine render_data = {};
		// Stored data hidden under the cursor
		std::vector<uint8_t> under_cursor = {};

		// Whether the Hercules hardware draws underline on this particular line
		bool hercules_underline = false;

		bool is_dirty   = true;
		bool has_cursor = false;

		uint32_t cursor_block = {};
		Rgb888 cursor_color   = {};
	};
	std::vector<CacheEntry> cache = {};

	void BackupAreaUnderCursor(CacheEntry& cache_entry);
	void RestoreAreaUnderCursor(CacheEntry& cache_entry);
};

static ScreenCache screen_cache;

static void draw_line(RenderDataLine& restrict render_data,
                      const uint8_t* vram_address,
                      const uint32_t render_line,
                      const bool hercules_underline);
static void draw_cursor(RenderDataLine& render_data,
                        const uint32_t cursor_block,
                        const Rgb888& cursor_color);

void ScreenCache::Configure(const uint32_t new_blocks_horizontal,
                            const uint32_t new_blocks_vertical,
                            const uint32_t new_block_width_px,
                            const uint32_t new_block_height_px)
{
	if (blocks_horizontal == new_blocks_horizontal &&
	    blocks_vertical == new_blocks_vertical &&
	    block_width_px == new_block_width_px &&
	    block_height_px == new_block_height_px) {

		// Same values, keep the existing cache
		return;
	}

	// We have a new screen dimensions
	FreeMemory();

	blocks_horizontal = new_blocks_horizontal;
	blocks_vertical   = new_blocks_vertical;

	block_width_px  = new_block_width_px;
	block_height_px = new_block_height_px;

	render_width_px  = blocks_horizontal * block_width_px;
	render_height_px = blocks_vertical * block_height_px;

	cached_video_memory.resize(blocks_vertical,
	                           std::vector<uint8_t>(blocks_horizontal * 2));

	cache.resize(render_height_px);
	for (auto& cache_entry : cache) {
		cache_entry.under_cursor.resize(block_width_px * BytesPerPixel);
	}
}

void ScreenCache::FreeMemory()
{
	cached_video_memory.clear();

	cache.clear();

	blocks_horizontal = 0;
	blocks_vertical   = 0;
	block_width_px    = 0;
	block_height_px   = 0;

	render_width_px  = 0;
	render_height_px = 0;
}

void ScreenCache::MarkAllDirty()
{
	for (auto& cache_entry : cache) {
		cache_entry.is_dirty = true;
	}
}

void ScreenCache::MarkRenderLineDirty(const uint32_t render_line)
{
	cache[render_line].is_dirty = true;
}

void ScreenCache::MarkBlockLineDirty(const uint32_t block_line)
{
	const uint32_t start = block_height_px * block_line;
	const uint32_t end   = start + block_height_px;

	for (uint32_t idx = start; idx < end; ++idx) {
		cache[idx].is_dirty = true;
	}
}

bool ScreenCache::IsLineDirty(const uint32_t render_line,
                              const bool hercules_underline) const
{
	if ((hercules_underline && !cache[render_line].hercules_underline) ||
	    (!hercules_underline && cache[render_line].hercules_underline)) {
		return true;
	}

	return cache[render_line].is_dirty;
}

void ScreenCache::UpdateLine(const uint8_t* vram_address,
                             const uint32_t render_line,
                             const bool hercules_underline)
{
	draw_line(cache[render_line].render_data,
	          vram_address,
	          render_line,
	          hercules_underline);

	cache[render_line].hercules_underline = hercules_underline;

	cache[render_line].has_cursor = false;
	cache[render_line].is_dirty   = false;
}

void ScreenCache::BackupAreaUnderCursor(CacheEntry& cache_entry)
{
	auto source = cache_entry.render_data.data() +
	              cache_entry.cursor_block * block_width_px * BytesPerPixel;
	auto destination = cache_entry.under_cursor.data();

	std::memcpy(destination, source, block_width_px * BytesPerPixel);
}

void ScreenCache::RestoreAreaUnderCursor(CacheEntry& cache_entry)
{
	if (!cache_entry.has_cursor) {
		return;
	}

	auto source = cache_entry.under_cursor.data();
	auto destination = cache_entry.render_data.data() +
	                   cache_entry.cursor_block * block_width_px * BytesPerPixel;

	std::memcpy(destination, source, block_width_px * BytesPerPixel);

	cache_entry.has_cursor = false;
}

const RenderDataLine& ScreenCache::GetRenderData(const uint32_t render_line,
                                                 bool& is_line_dirty)
{
	auto& cache_entry = cache[render_line];

	if (cache_entry.has_cursor) {
		is_line_dirty = true;
		RestoreAreaUnderCursor(cache_entry);
	}

	return cache_entry.render_data;
}

const RenderDataLine& ScreenCache::GetRenderData(const uint32_t render_line,
                                                 const uint32_t new_cursor_block,
                                                 const Rgb888& new_cursor_color,
                                                 bool& is_line_dirty)
{
	auto& cache_entry = cache[render_line];

	if (cache_entry.has_cursor &&
	    cache_entry.cursor_block == new_cursor_block &&
	    cache_entry.cursor_color == new_cursor_color) {
		// The cursor is already drawn, color and location matches
		return cache_entry.render_data;
	}

	is_line_dirty = true;
	RestoreAreaUnderCursor(cache_entry);

	cache_entry.cursor_block = new_cursor_block;
	cache_entry.cursor_color = new_cursor_color;

	BackupAreaUnderCursor(cache_entry);
	draw_cursor(cache_entry.render_data, new_cursor_block, new_cursor_color);

	cache_entry.has_cursor = true;

	return cache_entry.render_data;
}

void ScreenCache::UpdateColors(const ColorLookupTable& new_colors_foreground,
                               const ColorLookupTable& new_colors_background)
{
	if (cached_colors_foreground == new_colors_foreground &&
	    cached_colors_background == new_colors_background) {
		return;
	}

	cached_colors_foreground = new_colors_foreground;
	cached_colors_background = new_colors_background;

	MarkAllDirty();
}

void ScreenCache::UpdateVideoMemory(const uint8_t* vram_address,
                                    const uint32_t render_line)
{
	const uint32_t block_line = render_line / block_height_px;

	const auto cached_vram = cached_video_memory[block_line].data();
	const auto count_bytes = blocks_horizontal * 2;

	if (0 == std::memcmp(vram_address, cached_vram, count_bytes)) {
		return;
	}

	std::memcpy(cached_vram, vram_address, count_bytes);

	MarkBlockLineDirty(block_line);
}

#endif

// ***************************************************************************
// Rendering setup
// ***************************************************************************

static bool is_window_ready()
{
	return (GFX_GetWindow() != nullptr);
}

static bool is_supported_text_mode()
{
	// M_CGA_TEXT_COMPOSITE is not supported for now
	return (vga.mode == M_TEXT) || (vga.mode == M_TANDY_TEXT) ||
	       (vga.mode == M_HERC_TEXT);
}

bool TTF_ShouldOverrideScreen()
{
	const bool wants_override = is_initialized &&
	                            is_ttf_enabled &&
	                            is_window_ready() &&
	                            is_supported_text_mode() &&
	                            !ReelMagic_IsVideoMixerEnabled() &&
	                            // This should be the last condition, as
	                            // it is the most time expensive to check
	                            screen_font.IsCompatible();

	// Tell the other part whether the periodic VRAM font check is needed
	vga.draw.ttf.keep_checking_vram_font = wants_override;

	return wants_override && is_screen_font_unaltered && is_screen_size_sane;
}

static void check_if_screen_font_is_unaltered()
{
	constexpr uint8_t MaxBlockHeightPx = 32;

	if (!is_machine_ega_or_better()) {
		// It's not possible to alter screen font on pre-EGA hardware
		is_screen_font_unaltered = true;
		return;
	}

	// If VRAM is unchanged, skip the font check
	if (!vga.draw.ttf.vram_font_dirty_flag) {
		return;
	}
	vga.draw.ttf.vram_font_dirty_flag = false;

	// ROM vs VRAM font compare, for 8x14 and 8x16 fonts
	auto check_font = [&](const uint8_t block_size,
	                      const uint8_t* vram_font,
	                      const PhysPt rom_font,
	                      const PhysPt rom_font_alternate) {
		auto    phys_alternate	 = rom_font_alternate;
		uint8_t block_alternate  = phys_readb(phys_alternate++);

		const auto is_vga_9dot_font = is_machine_vga_or_better() &&
		                              !vga.seq.clocking_mode.is_eight_dot_mode;

		for (uint16_t block = 0; block < 256; ++block) {
			// Check if we need to use the alternate font for this block;
			// 0 marks the end of alternate font definitions
			bool use_alternate = is_vga_9dot_font &&
			                     (block_alternate != 0) &&
			                     (block_alternate == block);

			for (uint8_t line = 0; line < block_size; ++line) {
				const auto vram_value = *(vram_font + block * MaxBlockHeightPx + line);
				const auto rom_value  = use_alternate
					? phys_readb(phys_alternate++)
					: phys_readb(rom_font + block * block_size + line);

				if (rom_value != vram_value) {
					return false;
				}
			}

			if (use_alternate) {
				block_alternate = phys_readb(phys_alternate++);
			}
		}
		return true;
	};

	// ROM vs VRAM font compare, for 8x8 font
	auto check_font_8 = [&](const uint8_t* vram_font,
	                        const PhysPt rom_font,
	                        const bool second_part) {
		const uint8_t offset = second_part ? 128 : 0;

		for (uint16_t block = 0; block < 128; ++block) {
			for (uint8_t line = 0; line < 8; ++line) {
				const auto vram_value = *(vram_font + (block + offset) * MaxBlockHeightPx + line);
				const auto rom_value  = phys_readb(rom_font + block * 8 + line);

				if (rom_value != vram_value) {
					return false;
				}
			}
		}
		return true;
	};

	const auto vram_font_0 = vga.draw.font_tables[0];
	const auto vram_font_1 = vga.draw.font_tables[1];

	switch (vga.draw.address_line_total) {
	case 16: {
		const auto rom_font           = RealToPhysical(int10.rom.font_16);
		const auto rom_font_alternate = RealToPhysical(int10.rom.font_16_alternate);

		is_screen_font_unaltered =
			check_font(16, vram_font_0, rom_font, rom_font_alternate) &&
			check_font(16, vram_font_1, rom_font, rom_font_alternate);
		break;
	}
	case 14: {
		const auto rom_font           = RealToPhysical(int10.rom.font_14);
		const auto rom_font_alternate = RealToPhysical(int10.rom.font_14_alternate);

		is_screen_font_unaltered =
			check_font(14, vram_font_0, rom_font, rom_font_alternate) &&
			check_font(14, vram_font_1, rom_font, rom_font_alternate);
		break;
	}
	case 8: {
		constexpr bool First_8x8_Part  = false;
		constexpr bool Second_8x8_Part = true;

		const auto rom_font_8_first  = RealToPhysical(int10.rom.font_8_first);
		const auto rom_font_8_second = RealToPhysical(int10.rom.font_8_second);

		is_screen_font_unaltered =
		        check_font_8(vram_font_0, rom_font_8_first, First_8x8_Part) &&
		        check_font_8(vram_font_1, rom_font_8_first, First_8x8_Part) &&
		        check_font_8(vram_font_0, rom_font_8_second, Second_8x8_Part) &&
		        check_font_8(vram_font_1, rom_font_8_second, Second_8x8_Part);
		break;
	}
	default:
		// Other font height - no standard font available
		is_screen_font_unaltered = false;
		break;
	}
}

static void check_if_screen_size_is_sane()
{
	// Do not report anything suspicious if we don't support this screen mode
	if (!is_supported_text_mode()) {
		is_screen_size_sane = true;
		return;
	}

	is_screen_size_sane =
		(vga.draw.ttf.blocks_vertical <= MaxSupportedBlocksVertical) &&
		(vga.draw.ttf.blocks_horizontal <= MaxSupportedBlocksHorizontal);
}

bool TTF_ShouldChangeScreenOverride()
{
	// Mid-frame mode change: the new font would be compared at the old
	// character height. setup_drawing() checks again once it is applied.
	if (vga.draw.resizing) {
		return false;
	}

	check_if_screen_size_is_sane();

	if (!vga.draw.ttf.keep_checking_vram_font) {
		return false;
	}

	if (is_screen_size_sane) {
		const auto old_is_screen_font_unaltered = is_screen_font_unaltered;
		check_if_screen_font_is_unaltered();
		if (!is_screen_font_unaltered && old_is_screen_font_unaltered) {
			augra::log_info("ttf", "Screen font altered, disabling TTF output");
		} else if (is_screen_font_unaltered && !old_is_screen_font_unaltered) {
			augra::log_info("ttf", "Screen font restored, reenabling TTF output");
		}
	}

	const bool condition = is_screen_size_sane && is_screen_font_unaltered;
	return (vga.draw.ttf.override && !condition) ||
	       (!vga.draw.ttf.override && condition);
}

static std::pair<uint32_t, uint32_t> get_viewport_px()
{
	const auto viewport_px = GFX_GetViewportSizeInPixels();

	const auto x1_px = viewport_px.x1();
	const auto x2_px = viewport_px.x2();
	const auto y1_px = viewport_px.y1();
	const auto y2_px = viewport_px.y2();

	const uint32_t window_width_px  = round_to_uint32(x2_px - x1_px);
	const uint32_t window_height_px = round_to_uint32(y2_px - y1_px);

	return {std::max(1u, window_width_px), std::max(1u, window_height_px)};
}

static void shrink_to_ratio(uint32_t& width_px, uint32_t& height_px,
                            const float target_ratio)
{
	const float original_ratio = static_cast<float>(width_px) /
	                             static_cast<float>(height_px);

	if (original_ratio > target_ratio) {
		// We need to shrink the image horizontally
		const auto value = target_ratio * static_cast<float>(height_px);

		width_px = round_to_uint32(value);

	} else if (original_ratio < target_ratio) {
		// We need to shring the image vertically
		const auto value = static_cast<float>(width_px) / target_ratio;

		height_px = round_to_uint32(value);
	}
}

static void expand_proportionally(uint32_t& width_px, uint32_t& height_px,
                                  const uint32_t min_width_px,
                                  const uint32_t min_height_px)
{
	// Sanity checks
	assert(width_px > 0);
	assert(height_px > 0);
	assert(min_width_px > 0);
	assert(min_height_px > 0);

	if (width_px == 0 || height_px == 0 || min_width_px == 0 || min_height_px == 0) {
		return;
	}

	// No adjustment if already within limits
	if (width_px >= min_width_px && height_px >= min_height_px) {
		return;
	}

	// Determine the coefficient to expand by
	const float coefficient_horizontal = static_cast<float>(min_width_px) /
	                                     static_cast<float>(width_px);
	const float coefficient_vertical   = static_cast<float>(min_height_px) /
	                                     static_cast<float>(height_px);

	const float coefficient = std::max(coefficient_horizontal,
	                                   coefficient_vertical);

	// Expand the dimensions
	width_px  = round_to_uint32(static_cast<float>(width_px) * coefficient);
	height_px = round_to_uint32(static_cast<float>(height_px) * coefficient);

	// Correct the possible rounding errors
	width_px  = std::max(width_px, min_width_px);
	height_px = std::max(height_px, min_height_px);
}

static void shrink_proportionally(uint32_t& width_px, uint32_t& height_px,
                                  const uint32_t max_width_px,
                                  const uint32_t max_height_px)
{
	// Sanity checks
	assert(width_px > 0);
	assert(height_px > 0);
	assert(max_width_px > 0);
	assert(max_height_px > 0);

	if (width_px == 0 || height_px == 0 || max_width_px == 0 ||
	    max_height_px == 0) {
		return;
	}

	// No adjustment if already within limits
	if (width_px <= max_width_px && height_px <= max_height_px) {
		return;
	}

	// Determine the coefficient to shrink by
	const float coefficient_horizontal = static_cast<float>(width_px) /
	                                     static_cast<float>(max_width_px);
	const float coefficient_vertical   = static_cast<float>(height_px) /
	                                     static_cast<float>(max_height_px);

	const float coefficient = std::max(coefficient_horizontal,
	                                   coefficient_vertical);

	// Shrink the dimensions
	width_px  = round_to_uint32(static_cast<float>(width_px) / coefficient);
	height_px = round_to_uint32(static_cast<float>(height_px) / coefficient);

	// Correct the possible rounding errors
	width_px  = std::min(width_px, max_width_px);
	height_px = std::min(height_px, max_height_px);
}

void TTF_CalculateRenderSize(uint32_t& render_width_px, uint32_t& render_height_px,
                             Fraction& pixel_aspect_ratio)
{
	// We are going to try to enforce some minimum block size, so that glyph
	// tweaking routines can work reliably
	constexpr uint32_t MinBlockSizePx = 12;
	// The higher the value, the more 'sticky' the widescreen friendly
	// aspect ratio strategy is
	constexpr float WideRatioStickyThreshold = 0.04f;

	// Number of characters for the standard DOS screen mode
	constexpr uint32_t StandardBlocksHorizontal = 80;
	// 4:3 aspect ratio floating point constant
	constexpr float AspectRatio43 = 4.0f / 3.0f;

	// Calculate available viewport size (window resolution)
	const auto [viewport_width_px, viewport_height_px] = get_viewport_px();

	const float viewport_ratio = static_cast<float>(viewport_width_px) /
	                             static_cast<float>(viewport_height_px);

	// Get the maximum resolution supported by the scaler
	const uint32_t max_width_px  = ScalerMaxWidthTtf;
	const uint32_t max_height_px = ScalerMaxHeightTtf;

	// Calculate number of blocks (characters) in each line.columnt
	const uint32_t blocks_horizontal = vga.draw.ttf.blocks_horizontal;
	const uint32_t blocks_vertical   = vga.draw.ttf.blocks_vertical;

	// If we should select the aspect ration in a widescreen-friendly way
	const bool is_wide_friendly_mode = (aspect_mode == AspectMode::Wide) ||
	                                   (aspect_mode == AspectMode::WideSticky);
	// If we should stretch the image to the entire window
	bool is_strech_mode = (RENDER_GetAspectRatioCorrectionMode() ==
	                       AspectRatioCorrectionMode::Stretch);
	// If we should create a standard 4:3 image
	bool pixel_ratio_for_43 = false;

	// Calculate target image ratio
	float target_ratio = AspectRatio43;
	if (is_strech_mode) {
		target_ratio = viewport_ratio;

	} else if (is_wide_friendly_mode &&
	           blocks_horizontal > StandardBlocksHorizontal) {
		target_ratio *= static_cast<float>(blocks_horizontal) /
		                static_cast<float>(StandardBlocksHorizontal);

		// Try not to exceed the viewport ratio
		target_ratio = std::min(target_ratio, viewport_ratio);

		if (aspect_mode == AspectMode::WideSticky) {
			// Make the ratio a little sticky to avoid thin black
			// borders
			const auto ratio1 = target_ratio / viewport_ratio;
			const auto ratio2 = viewport_ratio / target_ratio;
			if (std::fabs(ratio1 - 1.0f) < WideRatioStickyThreshold ||
			    std::fabs(ratio2 - 1.0f) < WideRatioStickyThreshold) {
				// The target ratio is very close to the
				// viewport ratio; make them equal
				target_ratio   = viewport_ratio;
				is_strech_mode = true;
			}
		}

		// Make sure ration is at least the standard 4:3
		target_ratio = std::max(target_ratio, AspectRatio43);

	} else if (aspect_mode == AspectMode::Font) {
		// Detect font aspect ratio
		target_ratio = screen_font.GetReportedAspectRatio() *
		               static_cast<float>(blocks_horizontal) /
		               static_cast<float>(blocks_vertical);

	} else {
		// We want to produce a standard 4:3 image
		pixel_ratio_for_43 = true;
	}

	// Initial desired image resolution
	uint32_t target_width_px  = viewport_width_px;
	uint32_t target_height_px = viewport_height_px;

	// Scale dimensions down to fit into the target aspect ratio
	shrink_to_ratio(target_width_px, target_height_px, target_ratio);

	// If the resolution is small, enlarge the block dimensions a bit
	expand_proportionally(target_width_px,
	                      target_height_px,
	                      MinBlockSizePx * blocks_horizontal,
	                      MinBlockSizePx * blocks_vertical);

	// Shrink the image resolution so that it does exceed the maximum
	// dimensions supported by the scaler
	shrink_proportionally(target_width_px, target_height_px, max_width_px, max_height_px);

	// Calculate block (character) size in pixels
	const uint32_t block_width_px  = target_width_px / blocks_horizontal;
	const uint32_t block_height_px = target_height_px / blocks_vertical;

	// Adapt to the new block size
	screen_font.PreRenderBlocks(block_width_px, block_height_px);

	// Pupulate calculation results
	vga.draw.ttf.block_width  = block_width_px;
	vga.draw.ttf.block_height = block_height_px;

	render_width_px  = blocks_horizontal * block_width_px;
	render_height_px = blocks_vertical * block_height_px;

	if (is_strech_mode) {
		// Adjust the scaler pixel aspect ratio to make the image fit the
		// viewport perfectly - without any possible tiny black borders
		const auto render_ratio = Fraction(render_width_px, render_height_px);
		const auto window_ratio = Fraction(viewport_width_px,
		                                   viewport_height_px);

		pixel_aspect_ratio = render_ratio.Inverse() * window_ratio;

	} else if (pixel_ratio_for_43) {
		// Adjust the scaler pixel aspect ratio for a perfect 4:3 image
		// ratio
		const auto render_ratio = Fraction(render_width_px, render_height_px);
		const auto normal_ratio = Fraction(4, 3);

		pixel_aspect_ratio = render_ratio.Inverse() * normal_ratio;

	} else {
		// The image aspect ratio is not important - no correction needed
		pixel_aspect_ratio = 1;
	}

#ifndef DEBUG_TTF_NO_SCREEN_CACHE
	screen_cache.Configure(blocks_horizontal,
	                       blocks_vertical,
	                       vga.draw.ttf.block_width,
	                       vga.draw.ttf.block_height);
#endif
}

void TTF_FreeCacheMemory()
{
#ifndef DEBUG_TTF_NO_SCREEN_CACHE
	screen_cache.FreeMemory();
#endif
}

// ***************************************************************************
// Screen drawing
// ***************************************************************************

// Foreground/background colors for all the possible character attribute
// values; precalculated to speed up the rendering
static ColorLookupTable lookup_colors_foreground = {};
static ColorLookupTable lookup_colors_background = {};

// Foreground/background colors for every pixel column of the current row of
// blocks; precalculated at the start of every row of blocks to speed up
// the colorization step
alignas(SimdAlignment) static RenderDataLine row_colors_foreground = {};
alignas(SimdAlignment) static RenderDataLine row_colors_background = {};

// Characters in the row where the Hercules should draw underline
static std::array<bool, MaxSupportedBlocksHorizontal> row_hercules_underline = {};

static void maybe_recalculate_drawing()
{
	if (!is_initialized || !is_window_ready() || !is_supported_text_mode()) {
		return;
	}

#ifndef DEBUG_TTF_NO_SCREEN_CACHE
	screen_cache.MarkAllDirty();
#endif

	PIC_RemoveEvents(VGA_SetupDrawing);
	PIC_AddEvent(VGA_SetupDrawing, 0);
}

uint32_t TTF_HardwareLine(const uint32_t render_line)
{
	float line_float = static_cast<float>(render_line);
	line_float /= static_cast<float>(vga.draw.ttf.block_height);
	line_float *= static_cast<float>(vga.draw.address_line_total);

	return round_to_uint32(line_float);
}

void TTF_DrawPrepareScreen()
{
	// Prepare color lookup tables to slightly speed up the screen rendering
	for (auto color = 0; color < UINT8_MAX + 1; ++color) {
		lookup_colors_foreground[color] =
		        render.palette.rgb[TXT_FG_Table[color & 0x0f] % 0x100];
		lookup_colors_background[color] =
		        render.palette.rgb[TXT_BG_Table[color >> 4] % 0x100];
	}

#ifndef DEBUG_TTF_NO_SCREEN_CACHE
	screen_cache.UpdateColors(lookup_colors_foreground, lookup_colors_background);
#endif
}

static void prepare_block_color(uint32_t& pixel,
                                const uint8_t color_index,
                                RenderDataLine& render_foreground,
                                RenderDataLine& render_background)
{
	const auto& color_foreground = lookup_colors_foreground[color_index];
	const auto& color_background = lookup_colors_background[color_index];

	for (uint32_t idx = 0; idx < vga.draw.ttf.block_width; ++idx) {

		render_foreground[pixel] = color_foreground.blue;
		render_background[pixel] = color_background.blue;
		++pixel;

		render_foreground[pixel] = color_foreground.green;
		render_background[pixel] = color_background.green;
		++pixel;

		render_foreground[pixel] = color_foreground.red;
		render_background[pixel] = color_background.red;
		++pixel;

		static_assert(BytesPerPixel >= 3);
		for (uint8_t byte = 3; byte < BytesPerPixel; ++byte) {
			render_foreground[pixel] = 0;
			render_background[pixel] = 0;
			++pixel;
		}
	}
}

static void prepare_block_line(const uint8_t* vram_address)
{
	uint32_t pixel = 0;
	for (uint32_t block = 0; block < vga.draw.blocks; ++block) {
		const auto color = vram_address[block * 2 + 1];

		prepare_block_color(pixel,
		                    color,
		                    row_colors_foreground,
		                    row_colors_background);
	}
}

static void prepare_block_line_hercules(const uint8_t* vram_address)
{
	const uint8_t ColorIndexBlack = 0x00;
	const uint8_t ColorIndexGrey  = 0x07;
	const uint8_t ColorIndexWhite = 0x0f;

	uint32_t pixel = 0;
	for (uint32_t block = 0; block < vga.draw.blocks; ++block) {
		row_hercules_underline[block] = false;

		const auto attributes = vram_address[block * 2 + 1];

		if (!(attributes & 0x77)) {
			// 00h, 80h, 08h, 88h produce black space
			prepare_block_color(pixel,
			                    ColorIndexBlack + (ColorIndexBlack << 4),
			                    row_colors_foreground,
			                    row_colors_background);
			continue;
		}

		if ((attributes & 0x77) == 0x70) {
			if (attributes & 0x08) {
				prepare_block_color(pixel,
				                    ColorIndexWhite + (ColorIndexGrey << 4),
				                    row_colors_foreground,
				                    row_colors_background);
			} else {
				prepare_block_color(pixel,
				                    ColorIndexBlack + (ColorIndexGrey << 4),
				                    row_colors_foreground,
				                    row_colors_background);
			}
		} else {
			if ((attributes & 0x77) == 0x1) {
				row_hercules_underline[block] = true;
			}

			if (attributes & 0x08) {
				prepare_block_color(pixel,
				                    ColorIndexWhite + (ColorIndexBlack << 4),
				                    row_colors_foreground,
				                    row_colors_background);
			} else {
				prepare_block_color(pixel,
				                    ColorIndexGrey + (ColorIndexBlack << 4),
				                    row_colors_foreground,
				                    row_colors_background);
			}
		}
	}
}

void TTF_DrawPrepareBlockLine(const uint8_t* vram_address, const uint32_t render_line)
{
	if (render_line % vga.draw.ttf.block_height != 0 ||
	    render_line >= vga.draw.ttf.blocks_vertical * vga.draw.ttf.block_height) {

		// Nothing to do
		return;
	}

	// Prepare BGR color values for SIMD processing;
	// this needs to be done once per row of blocks
	if (vga.mode == M_HERC_TEXT) {
		prepare_block_line_hercules(vram_address);
	} else {
		prepare_block_line(vram_address);
	}

#ifndef DEBUG_TTF_NO_SCREEN_CACHE
	// The cache can be empty or smaller mid-frame (ada-a08x)
	if (render_line < screen_cache.GetRenderHeight()) {
		screen_cache.UpdateVideoMemory(vram_address, render_line);
	}
#endif
}

static void draw_line(RenderDataLine& restrict render_data,
                      const uint8_t* vram_address,
                      const uint32_t render_line,
                      const bool hercules_underline)
{
	const uint32_t line_in_block = render_line % vga.draw.ttf.block_height;

	// Render the line in BRG format, but in greyscale
	for (uint32_t block = 0; block < vga.draw.blocks; ++block) {
		const auto character = vram_address[block * 2];

		const auto offset = vga.draw.ttf.block_width * block * BytesPerPixel;

		if (hercules_underline && row_hercules_underline[block]) {
			// Draw the underline, font is not important here
			const uint32_t limit = vga.draw.ttf.block_width * BytesPerPixel;
			for (uint32_t pixel = 0; pixel < limit; ++pixel) {
				*(render_data.data() + offset + pixel) = UINT8_MAX;
			}
		} else {
			// Draw the font
			screen_font.RenderInGrey(render_data.data() + offset,
			                         character,
			                         line_in_block);
		}
	}

	// Now apply foreground/background colors. On high resolution screens
	// this is computationally heavy, but the code was written in such a way
	// that it should be easy for the compiler to vectorize it:
	// - all the input/output collections are arrays (their size is known at
	//   compile time), memory aligned for the AVX instruction set (since this
	//   function is not visible outside of the translation unit, the compiler
	//   can see all the potential input/output arrays are aligned), and the
	//   output is marked as restrict to inform the compiler it does not
	//   overlap with the inputs
	// - same array index is used for every value of input and output data;
	//   there are no cross-index dependencies
	// - the loop only uses explicitly specified 8- or 16-bit integers
	// - the loop mainly uses additions, substractions, and multiplications;
	//   the only division is by 0x100, which can be implemented as a bit
	//   shift
	// - the loop to be vectorized is marked as such using the OpenMP pragma
	auto colorize = [](const uint8_t color_fg,
	                   const uint8_t color_bg,
	                   const uint16_t value) {
		constexpr uint16_t MaxValue     = 0x100;
		constexpr uint16_t RoundingBias = MaxValue / 2;

		const uint16_t value_fg = value + 1;
		const uint16_t value_bg = MaxValue - value_fg;

		const uint16_t result = clamp_to_uint16(value_fg * color_fg) +
		                        clamp_to_uint16(value_bg * color_bg);

		return clamp_to_uint8((result + RoundingBias) / MaxValue);
	};

	const uint32_t limit = vga.draw.blocks * vga.draw.ttf.block_width * BytesPerPixel;

	#pragma omp simd
	for (uint32_t idx = 0; idx < limit; ++idx) {
		render_data[idx] = colorize(row_colors_foreground[idx],
		                            row_colors_background[idx],
		                            render_data[idx]);
	}
}

static void draw_cursor(RenderDataLine& render_data,
                        const uint32_t cursor_block, const Rgb888& cursor_color)
{
	auto position = render_data.data() +
	                cursor_block * vga.draw.ttf.block_width * BytesPerPixel;

	for (uint32_t idx = 0; idx < vga.draw.ttf.block_width; ++idx) {
		*(position++) = cursor_color.blue;
		*(position++) = cursor_color.green;
		*(position++) = cursor_color.red;
	}
}

static bool is_hercules_underline(const uint32_t render_line)
{
	if (!is_machine_hercules()) {
		return false;
	}

	const uint32_t render_block_line   = render_line % vga.draw.ttf.block_height;
	const uint32_t hardware_block_line = TTF_HardwareLine(render_block_line);

	return (vga.crtc.underline_location & 0x1f) == hardware_block_line;
}

static const RenderDataLine EmptyLine = {0};

const uint8_t* TTF_DrawLine(const uint8_t* vram_address,
                            const uint32_t render_line,
                            bool& is_line_dirty)
{
	is_line_dirty = false;

	if (render_line >= vga.draw.ttf.blocks_vertical * vga.draw.ttf.block_height) {
		is_line_dirty = true;
		return EmptyLine.data();
	}

	const auto hercules_underline = is_hercules_underline(render_line);

#ifndef DEBUG_TTF_NO_SCREEN_CACHE

	if (render_line >= screen_cache.GetRenderHeight()) {
		return EmptyLine.data();
	}
	if (screen_cache.IsLineDirty(render_line, hercules_underline)) {
		is_line_dirty = true;
		screen_cache.UpdateLine(vram_address, render_line, hercules_underline);
	}

	return screen_cache.GetRenderData(render_line, is_line_dirty).data();

#else
	alignas(SimdAlignment) static RenderDataLine render_data = {};

	is_line_dirty = true;
	draw_line(render_data, vram_address, render_line, hercules_underline);

	return render_data.data();
#endif
}

const uint8_t* TTF_DrawLine(const uint8_t* vram_address,
                            const uint32_t render_line,
                            const uint32_t cursor_block,
                            const Rgb888& cursor_color,
                            bool& is_line_dirty)
{
	is_line_dirty = false;

	if (render_line >= vga.draw.ttf.blocks_vertical * vga.draw.ttf.block_height) {
		is_line_dirty = true;
		return EmptyLine.data();
	}

	const auto hercules_underline = is_hercules_underline(render_line);

#ifndef DEBUG_TTF_NO_SCREEN_CACHE

	if (render_line >= screen_cache.GetRenderHeight()) {
		return EmptyLine.data();
	}
	if (screen_cache.IsLineDirty(render_line, hercules_underline)) {
		is_line_dirty = true;
		screen_cache.UpdateLine(vram_address, render_line, hercules_underline);
	}

	return screen_cache
	        .GetRenderData(render_line, cursor_block, cursor_color, is_line_dirty)
	        .data();

#else
	alignas(SimdAlignment) static RenderDataLine render_data = {};

	is_line_dirty = true;
	draw_line(render_data, vram_address, render_line, hercules_underline);
	draw_cursor(render_data, cursor_block, cursor_color);

	return render_data.data();
#endif
}

// ***************************************************************************
// External notifications
// ***************************************************************************

void TTF_NotifyNewCodePage()
{
	const auto should_override_screen = TTF_ShouldOverrideScreen();

	if (should_override_screen && vga.draw.ttf.override) {
		// We have a new code page - update callibrated and pre-rendered
		// data
		screen_font.PreRenderBlocks(vga.draw.ttf.block_width,
		                            vga.draw.ttf.block_height);
		return;
	}

	if (!should_override_screen && !vga.draw.ttf.override) {
		// Nothing to do, TTF output should stay disabled
		return;
	}

	// Either we should take over or back away
	// Either way we need to re-create drawing configuration
	VGA_SetupDrawing(0);
}

void TTF_NotifyNewVideoMixerState()
{
	const auto should_override_screen = TTF_ShouldOverrideScreen();

	if ((vga.draw.ttf.override && !should_override_screen) ||
	    (!vga.draw.ttf.override && should_override_screen)) {

		// The ReelMagic video mixer state change requires us to take
		// over the rendering or back away
		VGA_SetupDrawing(0);
	}
}

// ***************************************************************************
// Configuration
// ***************************************************************************

// Check if the file is suitable for loading
static bool is_font_file_ok(const std_fs::path& candidate)
{
	std::error_code error_code = {};
	if (candidate.empty() || !std_fs::exists(candidate, error_code) || error_code) {
		return false;
	}

	if (std_fs::is_regular_file(candidate, error_code) && !error_code) {
		return true;
	}

	return false;
}

std_fs::path TTF_FindFontFile(const std_fs::path& root_path,
                              const std::string& file_name, const uint8_t max_depth)
{
	// error_code overloads only: status() throws on a symlink loop or a
	// vanished mount, and a range-for has no non-throwing increment
	std::error_code error_code = {};
	if (!std_fs::is_directory(root_path, error_code) || error_code) {
		return {};
	}

	constexpr auto IteratorOption = std_fs::directory_options::skip_permission_denied;
	const auto end = std_fs::directory_iterator();

	auto file_it = std_fs::directory_iterator(root_path, IteratorOption, error_code);
	for (; !error_code && file_it != end; file_it.increment(error_code)) {
		std::error_code entry_error = {};
		if (!file_it->is_regular_file(entry_error) || entry_error) {
			continue;
		}

#if defined(WIN32) || defined(MACOSX)
		// Windows, MacOS - case insensitive file name compare
		if (iequals(get_file_name_from_path(file_it->path()), file_name)) {
#else
		// Linux - sensitive file name compare
		if (get_file_name_from_path(file_it->path()) == file_name) {
#endif
			return file_it->path();
		}
	}

	if (max_depth == 0) {
		return {};
	}

	error_code.clear();
	auto dir_it = std_fs::directory_iterator(root_path, IteratorOption, error_code);
	for (; !error_code && dir_it != end; dir_it.increment(error_code)) {
		std::error_code entry_error = {};
		if (!dir_it->is_directory(entry_error) || entry_error) {
			continue;
		}

		const auto result = TTF_FindFontFile(dir_it->path(),
		                                     file_name,
		                                     max_depth - 1);
		if (!result.empty() && is_font_file_ok(result)) {
			return result;
		}
	}

	return {};
}

static std_fs::path find_default_font_file()
{
	const auto result = get_resource_path(ResourceDir, DefaultFont);
	if (result.empty()) {
		augra::log_error("ttf", "Could not find the default font");
	}
	return result;
}

static std_fs::path find_custom_font_file(const std::string& font_name)
{
	if (font_name.empty()) {
		return {};
	}

	// Return file name with standard font extension
	auto get_path_with_extension = [](const std_fs::path& path) {
		if (get_file_name_from_path(path).contains('.')) {
			return path;
		}

		const std::string DefaultExtension = ".ttf";

		return std_fs::path(path.string() + DefaultExtension);
	};

	// Check for the file precisely as specified in the config file
	const std_fs::path native_path = to_native_path(font_name);
	if (is_font_file_ok(native_path)) {
		return native_path;
	}

	// Maybe try with the standard extension added
	const bool has_extension = get_file_name_from_path(native_path).contains('.');
	if (!has_extension) {
		const auto native_path_extension = get_path_with_extension(native_path);
		if (is_font_file_ok(native_path_extension)) {
			return native_path_extension;
		}
	}
	// Check if we should try the standard system directories
	const std_fs::path font_file_name = font_name;
	if (std::distance(font_file_name.begin(), font_file_name.end()) == 1) {
		// We have a bare file name, without path - check the standard
		// font locations

		const auto directories = get_standard_font_dirs();
		for (const auto& directory : directories) {
			const auto result = TTF_FindFontFile(directory,
			                                     font_file_name.string());
			if (!result.empty()) {
				return result;
			}
		}

		if (!has_extension) {
			// Not found - try once again, with the standard
			// extension added
			const auto font_file_name_extension = get_path_with_extension(
			        font_file_name);
			for (const auto& directory : directories) {
				const auto result = TTF_FindFontFile(
				        directory,
				        font_file_name_extension.string());
				if (!result.empty()) {
					return result;
				}
			}
		}
	}

	augra::log_warn("ttf", "Could not find the '%s' font", font_name.c_str());
	return {};
}

void TTF_ReadConfigAspect(SectionProp& section)
{
	const auto old_value = aspect_mode;

	const auto aspect_mode_str = section.GetString("ttf_aspect");
	if (aspect_mode_str == OptionAspect::Original) {
		aspect_mode = AspectMode::Original;
	} else if (aspect_mode_str == OptionAspect::Wide) {
		aspect_mode = AspectMode::Wide;
	} else if (aspect_mode_str == OptionAspect::WideSticky) {
		aspect_mode = AspectMode::WideSticky;
	} else if (aspect_mode_str == OptionAspect::Font) {
		aspect_mode = AspectMode::Font;
	}

	if (old_value != aspect_mode) {
		maybe_recalculate_drawing();
	}
}

void TTF_ReadConfigFont(SectionProp& section)
{
	const auto old_value = screen_font_file;

	screen_font_file = section.GetString("ttf_font");
	if (is_initialized && old_value == screen_font_file) {
		return;
	}

	// Get the custom font path
	const auto custom_font_path = find_custom_font_file(screen_font_file);

	// Load the custom specified font
	if (!custom_font_path.empty() && screen_font.Load(custom_font_path)) {
		// Custom specified font loaded succesfully
		augra::log_info("ttf", "Loaded font '%s'",
		                custom_font_path.string().c_str());
		maybe_recalculate_drawing();
		return;
	}

	// Get the default font path
	const auto default_font_path = find_default_font_file();
	if (default_font_path.empty()) {
		maybe_recalculate_drawing();
		return;
	}

	// Load the default font
	if (screen_font.Load(default_font_path)) {
		augra::log_info("ttf", "Loaded default font '%s'", DefaultFont.c_str());
	}
	maybe_recalculate_drawing();
}

void TTF_ReadConfigOutput(SectionProp& section)
{
	const auto old_value = is_ttf_enabled;

	is_ttf_enabled = section.GetBool("ttf_output");

	if (is_ttf_enabled != old_value) {
		maybe_recalculate_drawing();
	}
}

static void read_config()
{
	const auto section = get_sdl_section();
	assert(section);
	if (section == nullptr) {
		return;
	}

	TTF_ReadConfigAspect(*section);
	TTF_ReadConfigOutput(*section);
	TTF_ReadConfigFont(*section);
}

void TTF_AddConfigOptions(SectionProp& section)
{
	using enum Property::Changeable::Value;

	auto pbool = section.AddBool("ttf_output", Always, false);
	pbool->SetHelp(
	        "Replace text mode output with TrueType font output ('off' by default).\n"
	        "\n"
	        "Notes:\n"
	        "  - CGA composite output is not supported.\n"
	        "\n"
	        "  - Code pages loaded from custom CPI files are not supported.\n"
	        "\n"
	        "  - Concrete code page support depends on the selected font.");

	auto pstring = section.AddString("ttf_aspect", Always, OptionAspect::Original);
	pstring->SetValues({OptionAspect::Original,
	                    OptionAspect::Wide,
	                    OptionAspect::WideSticky,
	                    OptionAspect::Font});
	pstring->SetHelp(
	        "Selects the TrueType font aspect ratio ('original' by default):\n"
	        "\n"
	        "  original:     Keeps the original CRT monitor 4:3 screen proportions (default).\n"
	        "\n"
	        "  wide:         Similar to 'original', but more widescreen friendly.\n"
	        "                For text modes above 80 characters wide the screen width is\n"
	        "                extended proportionally.\n"
	        "\n"
	        "  wide-sticky:  Similar to 'wide', but there is some stickiness in the aspect\n"
	        "                ratio selection to make it easier to fill-in the whole screen.\n"
	        "\n"
	        "  font:         Keeps the selected TrueType font proportions.\n"
	        "\n"
	        "Note: The 'aspect = stretch' setting overrides 'ttf_aspect'.");

	pstring = section.AddString("ttf_font", Always, "");
	pstring->SetHelp(
	        "Name of the TrueType font file to be used for text display (default empty).\n"
	        "The '.ttf' file extension can be omitted. If no path is provided, standard font\n"
	        "locations are searched in addition to the current directory. If empty, the\n"
	        "default bundled font is used.");
}

// ***************************************************************************
// Information retrieval
// ***************************************************************************

bool TTF_IsOverridingScreen()
{
	return vga.draw.ttf.override;
}

std::string TTF_GetLoadedScreenFont()
{
	return get_file_name_from_path(screen_font.LoadedFilePath());
}

std::string TTF_ShortenFontName(const std::string& font_name, const size_t max_length)
{
	const std::string TrimMark = "(...)";
	const auto target_length   = std::max(max_length, TrimMark.size());

	auto result = font_name;
	if (result.size() <= target_length) {
		// No need to trim the name
		return result;
	}

	// If no extension present, just trim the trailing part
	if (!result.contains('.')) {
		result.resize(target_length - TrimMark.size());
		return result + TrimMark;
	}

	// Split the file name into stem and extension
	const auto split_point = result.find_last_of('.');

	auto result_stem      = result.substr(0, split_point);
	auto result_extension = result.substr(split_point + 1);

	// If extension is really long, just trim the trailing part
	const auto extension_length   = result_extension.length();
	const auto target_stem_length = target_length - extension_length - 1;
	if ((TrimMark.length() > target_stem_length) ||
	    (TrimMark.length() + extension_length + 1 > max_length)) {
		result.resize(target_length - TrimMark.size());
		return result + TrimMark;
	}

	// Shorten the stem, keep the extension intact
	result_stem.resize(target_stem_length - TrimMark.size());
	return result_stem + TrimMark + '.' + result_extension;
}

// ***************************************************************************
// Lifecycle
// ***************************************************************************

void TTF_Init()
{
	if (is_initialized) {
		return;
	}

	if (!FreeType::Init()) {
		return;
	}

	read_config();
	is_initialized = true;
}

void TTF_Destroy()
{
	if (!is_initialized) {
		return;
	}

	screen_font.Unload();
#ifndef DEBUG_TTF_NO_SCREEN_CACHE
	screen_cache.FreeMemory();
#endif

	FreeType::Done();
	is_initialized = false;
}
