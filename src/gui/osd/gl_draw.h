// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_GUI_OSD_GL_DRAW_H
#define DOSBOX_GUI_OSD_GL_DRAW_H

#include "dosbox_config.h"

#if C_OPENGL

#include <cstdint>
#include <vector>

#include "gui/osd/draw_context.h"

namespace OSD {

// Converts a pixel-space rect (top-left origin) into two NDC triangles
// (y up) and appends them to `out` as 6 vertices of [x, y, r, g, b, a].
// Returns false without appending when the rect or output size is
// degenerate. Pure function so the conversion is unit-testable without
// a GL context.
bool AppendRectVertices(std::vector<float>& out, const Rect& rect, const Color& color,
                        int output_width_px, int output_height_px);

// Draws the OSD on the OpenGL backend, which has no SDL_Renderer.
// Batches all rects of a frame into one vertex buffer and draws them
// with a minimal flat-color shader over the presented frame.
//
// Requires the backend's GL context to be current during construction,
// destruction, and the BeginFrame/EndFrame bracket. Construction can
// fail (shader compile/link); the object then stays inert and IsValid()
// returns false.
class OpenGlDrawContext final : public DrawContext {
public:
	OpenGlDrawContext();
	~OpenGlDrawContext() override;

	bool IsValid() const;

	// Bracket one frame's OSD drawing between the frame capture and the
	// buffer swap. BeginFrame records the window size; GL state is only
	// touched once there is something to draw. EndFrame flushes and
	// restores the state it changed.
	void BeginFrame(int width_px, int height_px);
	void EndFrame();

	void FillRect(const Rect& rect, const Color& color) override;

	int OutputWidth() override;
	int OutputHeight() override;

	// prevent copying
	OpenGlDrawContext(const OpenGlDrawContext&)            = delete;
	OpenGlDrawContext& operator=(const OpenGlDrawContext&) = delete;

private:
	void EnsureDrawState();
	void FlushBatch();

	// Bounds host memory no matter how many rects the overlays produce;
	// the batch flushes and refills past this.
	static constexpr int MaxBatchRects = 4096;

	static constexpr int NumVerticesPerRect = 6;
	static constexpr int NumFloatsPerVertex = 6;

	bool valid             = false;
	bool draw_state_active = false;

	int output_width_px  = 0;
	int output_height_px = 0;

	// GL object handles (GLuint)
	uint32_t program = 0;
	uint32_t vao     = 0;
	uint32_t vbo     = 0;

	std::vector<float> batch = {};
	int batch_num_rects      = 0;
};

} // namespace OSD

#endif // C_OPENGL

#endif
