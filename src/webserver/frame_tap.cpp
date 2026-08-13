// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "webserver/private/frame_tap.h"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace Webserver {

FrameSource ParseFrameSource(const std::string& value)
{
	if (value.empty() || value == "raw") {
		return FrameSource::Raw;
	}
	if (value == "rendered") {
		return FrameSource::Rendered;
	}
	throw std::invalid_argument("mode must be 'raw' or 'rendered'");
}

// The GL backend reads the backbuffer bottom-up; consumers of the tap
// (the image encoders) expect top-down rows, so normalize here where
// the flip flag is still known to be trustworthy.
static void flip_rows_in_place(RenderedImage& image)
{
	if (image.pitch <= 0 || image.params.height < 2) {
		return;
	}
	const auto pitch = static_cast<size_t>(image.pitch);
	std::vector<uint8_t> row(pitch);

	auto* top    = image.image_data;
	auto* bottom = image.image_data +
	               static_cast<size_t>(image.params.height - 1) * pitch;

	while (top < bottom) {
		std::memcpy(row.data(), top, pitch);
		std::memcpy(top, bottom, pitch);
		std::memcpy(bottom, row.data(), pitch);
		top += pitch;
		bottom -= pitch;
	}
	image.is_flipped_vertically = false;
}

RenderedFrameTap::~RenderedFrameTap()
{
	if (has_frame) {
		frame.free();
	}
}

std::optional<RenderedImage> RenderedFrameTap::RequestAndWait(
        const std::chrono::milliseconds timeout)
{
	// One request in flight at a time; a concurrent caller queues up
	// here and issues its own request afterwards
	std::lock_guard serialize(request_serializer);

	std::unique_lock lock(state_mutex);
	has_frame = false;
	requested = true;

	const bool got = delivered_cv.wait_for(lock, timeout, [&] {
		return has_frame;
	});
	requested      = false;

	if (!got) {
		return std::nullopt;
	}

	has_frame   = false;
	auto result = frame;
	frame       = {};
	return result;
}

bool RenderedFrameTap::IsRequested() const
{
	return requested.load(std::memory_order_relaxed);
}

void RenderedFrameTap::Deliver(const RenderedImage& image)
{
	if (!requested.load(std::memory_order_relaxed)) {
		return;
	}

	std::lock_guard lock(state_mutex);
	if (!requested || has_frame) {
		return;
	}

	frame = image.deep_copy();
	if (frame.is_flipped_vertically) {
		flip_rows_in_place(frame);
	}
	has_frame = true;
	delivered_cv.notify_one();
}

RenderedFrameTap& GetRenderedFrameTap()
{
	static RenderedFrameTap tap;
	return tap;
}

bool RenderedFrameRequested()
{
	return GetRenderedFrameTap().IsRequested();
}

void DeliverRenderedFrame(const RenderedImage& image)
{
	GetRenderedFrameTap().Deliver(image);
}

} // namespace Webserver
