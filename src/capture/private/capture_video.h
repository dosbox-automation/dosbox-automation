// SPDX-FileCopyrightText:  2023-2025 The DOSBox Staging Team
// SPDX-FileCopyrightText:  2002-2021 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_CAPTURE_VIDEO_H
#define DOSBOX_CAPTURE_VIDEO_H

#include "gui/render/render.h"
#include "misc/std_filesystem.h"

void capture_video_add_frame(const RenderedImage& image,
                             const float frames_per_second);

// Where the recording lands and how much free space must remain on it;
// a limit of 0 disables the proactive disk space check
void capture_video_set_free_space_limit(const std_fs::path& capture_dir,
                                        const uint32_t min_free_mb);

void capture_video_set_compression_levels(int raw_level, int rendered_level);

int capture_video_get_compression_level(bool rendered);
void capture_video_set_compression_level(bool rendered, int level);

void capture_video_add_audio_data(const uint32_t sample_rate,
                                  const uint32_t num_sample_frames,
                                  const int16_t* sample_frames);

void capture_video_finalise();

#endif
