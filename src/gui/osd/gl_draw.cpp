// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "gui/osd/gl_draw.h"

#if C_OPENGL

#include "misc/support.h"
#include "utils/checks.h"

// Glad must be included before SDL
#include "glad/gl.h"

CHECK_NARROWING();

namespace OSD {

bool AppendRectVertices(std::vector<float>& out, const Rect& rect, const Color& color,
                        const int output_width_px, const int output_height_px)
{
	if (rect.w <= 0 || rect.h <= 0) {
		return false;
	}
	if (output_width_px <= 0 || output_height_px <= 0) {
		return false;
	}

	const auto out_w = static_cast<float>(output_width_px);
	const auto out_h = static_cast<float>(output_height_px);

	// Pixel space has the origin at the top-left with y growing down;
	// NDC has the origin in the centre with y growing up.
	const auto x0 = 2.0f * static_cast<float>(rect.x) / out_w - 1.0f;
	const auto y0 = 1.0f - 2.0f * static_cast<float>(rect.y) / out_h;
	const auto x1 = 2.0f * static_cast<float>(rect.x + rect.w) / out_w - 1.0f;
	const auto y1 = 1.0f - 2.0f * static_cast<float>(rect.y + rect.h) / out_h;

	constexpr auto MaxChannel = 255.0f;

	const auto r = static_cast<float>(color.r) / MaxChannel;
	const auto g = static_cast<float>(color.g) / MaxChannel;
	const auto b = static_cast<float>(color.b) / MaxChannel;
	const auto a = static_cast<float>(color.a) / MaxChannel;

	const float vertices[] = {
	        // clang-format off
	        x0, y0, r, g, b, a,
	        x1, y0, r, g, b, a,
	        x0, y1, r, g, b, a,
	        x1, y0, r, g, b, a,
	        x1, y1, r, g, b, a,
	        x0, y1, r, g, b, a,
	        // clang-format on
	};

	out.insert(out.end(), std::begin(vertices), std::end(vertices));
	return true;
}

static const char* VertexShaderSource = R"(
#version 330 core
layout(location = 0) in vec2 position;
layout(location = 1) in vec4 color;
out vec4 vert_color;
void main() {
	vert_color  = color;
	gl_Position = vec4(position, 0.0, 1.0);
}
)";

static const char* FragmentShaderSource = R"(
#version 330 core
in vec4 vert_color;
out vec4 frag_color;
void main() {
	frag_color = vert_color;
}
)";

static GLuint compile_shader(const GLenum type, const char* source)
{
	const auto shader = glCreateShader(type);
	glShaderSource(shader, 1, &source, nullptr);
	glCompileShader(shader);

	GLint ok = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (ok != GL_TRUE) {
		char info_log[512] = {};
		glGetShaderInfoLog(shader, sizeof(info_log), nullptr, info_log);
		LOG_ERR("OPENGL: Error compiling OSD overlay shader: %s", info_log);

		glDeleteShader(shader);
		return 0;
	}
	return shader;
}

static GLuint build_program()
{
	const auto vertex_shader = compile_shader(GL_VERTEX_SHADER,
	                                          VertexShaderSource);
	if (!vertex_shader) {
		return 0;
	}

	const auto fragment_shader = compile_shader(GL_FRAGMENT_SHADER,
	                                            FragmentShaderSource);
	if (!fragment_shader) {
		glDeleteShader(vertex_shader);
		return 0;
	}

	const auto program = glCreateProgram();
	glAttachShader(program, vertex_shader);
	glAttachShader(program, fragment_shader);
	glLinkProgram(program);

	// The program keeps the compiled shaders alive; the handles are no
	// longer needed either way.
	glDeleteShader(vertex_shader);
	glDeleteShader(fragment_shader);

	GLint ok = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &ok);
	if (ok != GL_TRUE) {
		char info_log[512] = {};
		glGetProgramInfoLog(program, sizeof(info_log), nullptr, info_log);
		LOG_ERR("OPENGL: Error linking OSD overlay shader: %s", info_log);

		glDeleteProgram(program);
		return 0;
	}
	return program;
}

OpenGlDrawContext::OpenGlDrawContext()
{
	program = build_program();
	if (!program) {
		return;
	}

	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);

	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	constexpr auto StrideBytes = NumFloatsPerVertex * sizeof(float);

	glVertexAttribPointer(0, // position attribute
	                      2, // vec2
	                      GL_FLOAT,
	                      GL_FALSE, // no fixed-point normalisation
	                      StrideBytes,
	                      static_cast<GLvoid*>(nullptr));
	glEnableVertexAttribArray(0);

	glVertexAttribPointer(1, // color attribute
	                      4, // vec4
	                      GL_FLOAT,
	                      GL_FALSE, // no fixed-point normalisation
	                      StrideBytes,
	                      reinterpret_cast<GLvoid*>(2 * sizeof(float)));
	glEnableVertexAttribArray(1);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	batch.reserve(static_cast<size_t>(MaxBatchRects) * NumVerticesPerRect *
	              NumFloatsPerVertex);

	valid = true;
}

OpenGlDrawContext::~OpenGlDrawContext()
{
	if (vao) {
		glDeleteVertexArrays(1, &vao);
	}
	if (vbo) {
		glDeleteBuffers(1, &vbo);
	}
	if (program) {
		glDeleteProgram(program);
	}
}

bool OpenGlDrawContext::IsValid() const
{
	return valid;
}

void OpenGlDrawContext::BeginFrame(const int width_px, const int height_px)
{
	output_width_px  = width_px;
	output_height_px = height_px;

	batch.clear();
	batch_num_rects   = 0;
	draw_state_active = false;
}

void OpenGlDrawContext::EnsureDrawState()
{
	if (draw_state_active) {
		return;
	}

	// The shader pipeline sets its own viewport per pass, so covering
	// the full window here doesn't need restoring.
	glViewport(0, 0, output_width_px, output_height_px);

	// Blend is off everywhere else in the renderer; the OSD background
	// box is translucent, so enable it for the bracket and disable it
	// again in EndFrame.
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glUseProgram(program);
	glBindVertexArray(vao);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	draw_state_active = true;
}

void OpenGlDrawContext::FlushBatch()
{
	if (batch.empty()) {
		return;
	}

	EnsureDrawState();

	const auto num_vertices = batch.size() / NumFloatsPerVertex;

	glBufferData(GL_ARRAY_BUFFER,
	             check_cast<GLsizeiptr>(batch.size() * sizeof(float)),
	             batch.data(),
	             GL_STREAM_DRAW);

	glDrawArrays(GL_TRIANGLES, 0, check_cast<GLsizei>(num_vertices));

	batch.clear();
	batch_num_rects = 0;
}

void OpenGlDrawContext::FillRect(const Rect& rect, const Color& color)
{
	if (!valid) {
		return;
	}

	if (!AppendRectVertices(batch, rect, color, output_width_px, output_height_px)) {
		return;
	}

	if (++batch_num_rects >= MaxBatchRects) {
		FlushBatch();
	}
}

void OpenGlDrawContext::EndFrame()
{
	if (!valid) {
		return;
	}

	FlushBatch();

	if (draw_state_active) {
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
		glUseProgram(0);
		glDisable(GL_BLEND);

		draw_state_active = false;
	}
}

int OpenGlDrawContext::OutputWidth()
{
	return output_width_px;
}

int OpenGlDrawContext::OutputHeight()
{
	return output_height_px;
}

} // namespace OSD

#endif // C_OPENGL
