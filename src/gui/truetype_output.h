// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_TRUETYPE_OUTPUT_H
#define DOSBOX_TRUETYPE_OUTPUT_H

#include <cstddef>
#include <cstdint>
#include <string>

#include "config/config.h"
#include "misc/std_filesystem.h"
#include "utils/fraction.h"
#include "utils/rgb888.h"

// ***************************************************************************
// Rendering setup
// ***************************************************************************

// To be called within the screen mode setup to determine whether the TTF
// engine should takeover the rendering
bool TTF_ShouldOverrideScreen();

// To be called just before a frame is sent to the renderer, to determine
// whether we should re-setup the screen mode so that the TTF engine can
// take over or disengage
bool TTF_ShouldChangeScreenOverride();

// To be called during screen mode setup, when determined that the TTF engine
// wants to override screen rendering, to calculate the screen resolution
void TTF_CalculateRenderSize(uint32_t& render_width_px, uint32_t& render_height_px,
                             Fraction& pixel_aspect_ratio);

// Free the rendering cache memory
void TTF_FreeCacheMemory();

// ***************************************************************************
// Screen drawing
// ***************************************************************************

// Converts the line in terms of TTF rendering to the line on terms of emulated
// hardware
uint32_t TTF_HardwareLine(const uint32_t render_line);

// To be called at the start of each frame
void TTF_DrawPrepareScreen();

// To be called at the start of each row of text
void TTF_DrawPrepareBlockLine(const uint8_t* vram_address,
                              const uint32_t render_line);

// Actual line drawing. Sets the 'is_line_dirty' flag if it detects that the
// rendered image has probably changed, otherwise puts 'false' there
const uint8_t* TTF_DrawLine(const uint8_t* vram_address,
                            const uint32_t render_line,
                            bool& is_line_dirty);
const uint8_t* TTF_DrawLine(const uint8_t* vram_address,
                            const uint32_t render_line,
                            const uint32_t cursor_block,
                            const Rgb888& cursor_color,
                            bool& is_line_dirty);

// ***************************************************************************
// External notifications
// ***************************************************************************

// To be called after the new code page is loaded
void TTF_NotifyNewCodePage();
// To be called when the ReelMagic video mixer state changes
void TTF_NotifyNewVideoMixerState();

// ***************************************************************************
// Configuration
// ***************************************************************************

// Reads the updated configuration parameters
void TTF_ReadConfigAspect(SectionProp& section);
void TTF_ReadConfigFont(SectionProp& section);
void TTF_ReadConfigOutput(SectionProp& section);

// Add the config entries to the given section
void TTF_AddConfigOptions(SectionProp& section);

// ***************************************************************************
// Information retrieval
// ***************************************************************************

// Returns 'true' if the TTF engine has taken over the screen rendering
bool TTF_IsOverridingScreen();

// Get the file name of the loaded screen font.
std::string TTF_GetLoadedScreenFont();

// Tries to shorten the font name to the given length; might be unable to
// shorten it below 10 characters.
std::string TTF_ShortenFontName(const std::string& font_name, const size_t max_length);

// Searches root_path and up to max_depth directory levels below it for a
// font file of the given name. Returns an empty path if not found.
std_fs::path TTF_FindFontFile(const std_fs::path& root_path,
                              const std::string& file_name,
                              const uint8_t max_depth = 3);

// ***************************************************************************
// Lifecycle
// ***************************************************************************

void TTF_Init();
void TTF_Destroy();

#endif // DOSBOX_TRUETYPE_OUTPUT_H
