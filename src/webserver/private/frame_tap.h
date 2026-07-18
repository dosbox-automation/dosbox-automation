// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_WEBSERVER_FRAME_TAP_H
#define DOSBOX_WEBSERVER_FRAME_TAP_H

#include "misc/rendered_image.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>

namespace Webserver {

enum class FrameSource : uint8_t { Raw, Rendered };

// Parses the frame endpoint's "mode" parameter. Empty selects Raw.
// Throws std::invalid_argument on anything else.
FrameSource ParseFrameSource(const std::string& value);

// One-shot handoff of a rendered (post-shader) frame from the present
// path to a waiting webserver request. The present thread's cost while
// nothing is requested is a single relaxed atomic load.
class RenderedFrameTap {
public:
	RenderedFrameTap() = default;
	~RenderedFrameTap();

	RenderedFrameTap(const RenderedFrameTap&)            = delete;
	RenderedFrameTap& operator=(const RenderedFrameTap&) = delete;

	// Webserver thread. Blocks until a frame is delivered or the
	// timeout elapses. The returned image is owned by the caller,
	// stored top-down, and must be free()d. Concurrent callers are
	// serialized; each gets its own frame.
	std::optional<RenderedImage> RequestAndWait(std::chrono::milliseconds timeout);

	// Present thread. Cheap check whether a request is pending.
	bool IsRequested() const;

	// Present thread. Deep-copies the image for the waiter; the
	// caller keeps ownership of its own buffer. No-op when nothing
	// is requested.
	void Deliver(const RenderedImage& image);

private:
	std::mutex request_serializer        = {};
	std::mutex state_mutex               = {};
	std::condition_variable delivered_cv = {};
	std::atomic<bool> requested          = false;
	bool has_frame                       = false;
	RenderedImage frame                  = {};
};

RenderedFrameTap& GetRenderedFrameTap();

// Thin wrappers for the GUI layer, which forward-declares these
// instead of including webserver headers
bool RenderedFrameRequested();
void DeliverRenderedFrame(const RenderedImage& image);

} // namespace Webserver

#endif // DOSBOX_WEBSERVER_FRAME_TAP_H
