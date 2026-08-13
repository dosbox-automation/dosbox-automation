// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "webserver/private/frame_tap.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <thread>

using namespace std::chrono_literals;
using Webserver::FrameSource;
using Webserver::ParseFrameSource;
using Webserver::RenderedFrameTap;

namespace {

// 2x2 BGR24 image; row 0 = 0x10 bytes, row 1 = 0x20 bytes, so row order
// is observable after a flip
static RenderedImage make_test_image(const bool flipped)
{
	RenderedImage image = {};

	image.params.width          = 2;
	image.params.height         = 2;
	image.params.pixel_format   = PixelFormat::BGR24_ByteArray;
	image.pitch                 = 6;
	image.is_flipped_vertically = flipped;

	image.image_data = new uint8_t[12];
	std::memset(image.image_data, 0x10, 6);
	std::memset(image.image_data + 6, 0x20, 6);

	return image;
}

TEST(FrameSource, EmptyDefaultsToRaw)
{
	EXPECT_EQ(ParseFrameSource(""), FrameSource::Raw);
}

TEST(FrameSource, ParsesRaw)
{
	EXPECT_EQ(ParseFrameSource("raw"), FrameSource::Raw);
}

TEST(FrameSource, ParsesRendered)
{
	EXPECT_EQ(ParseFrameSource("rendered"), FrameSource::Rendered);
}

TEST(FrameSource, RejectsUnknownValue)
{
	EXPECT_THROW(ParseFrameSource("bogus"), std::invalid_argument);
}

TEST(FrameSource, RejectsWrongCase)
{
	EXPECT_THROW(ParseFrameSource("Rendered"), std::invalid_argument);
}

TEST(FrameSource, RejectsEmbeddedNul)
{
	const std::string tricky("raw\0raw", 7);
	EXPECT_THROW(ParseFrameSource(tricky), std::invalid_argument);
}

TEST(FrameSource, RejectsOversizedValue)
{
	EXPECT_THROW(ParseFrameSource(std::string(4096, 'a')), std::invalid_argument);
}

TEST(RenderedFrameTap, NotRequestedInitially)
{
	RenderedFrameTap tap;
	EXPECT_FALSE(tap.IsRequested());
}

TEST(RenderedFrameTap, DeliverWithoutRequestIsNoOp)
{
	RenderedFrameTap tap;
	auto image = make_test_image(false);

	tap.Deliver(image);

	// The tap must not have taken or freed the caller's buffer
	EXPECT_EQ(image.image_data[0], 0x10);
	image.free();
}

TEST(RenderedFrameTap, RequestTimesOutWithoutDelivery)
{
	RenderedFrameTap tap;

	const auto result = tap.RequestAndWait(50ms);

	EXPECT_FALSE(result.has_value());
	EXPECT_FALSE(tap.IsRequested());
}

TEST(RenderedFrameTap, DeliverFulfillsPendingRequest)
{
	RenderedFrameTap tap;

	std::thread producer([&] {
		while (!tap.IsRequested()) {
			std::this_thread::sleep_for(1ms);
		}
		auto image = make_test_image(false);
		tap.Deliver(image);
		image.free();
	});

	auto result = tap.RequestAndWait(2000ms);
	producer.join();

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->params.width, 2);
	EXPECT_EQ(result->params.height, 2);
	EXPECT_EQ(result->image_data[0], 0x10);
	EXPECT_EQ(result->image_data[6], 0x20);
	EXPECT_FALSE(tap.IsRequested());

	result->free();
}

TEST(RenderedFrameTap, DeliveredImageIsADeepCopy)
{
	RenderedFrameTap tap;

	auto source = make_test_image(false);

	std::thread producer([&] {
		while (!tap.IsRequested()) {
			std::this_thread::sleep_for(1ms);
		}
		tap.Deliver(source);
		// Mutate and free the source after delivery; the copy
		// handed to the waiter must be unaffected
		source.image_data[0] = 0xFF;
		source.free();
	});

	auto result = tap.RequestAndWait(2000ms);
	producer.join();

	ASSERT_TRUE(result.has_value());
	EXPECT_EQ(result->image_data[0], 0x10);

	result->free();
}

TEST(RenderedFrameTap, FlippedDeliveryIsNormalizedTopDown)
{
	RenderedFrameTap tap;

	std::thread producer([&] {
		while (!tap.IsRequested()) {
			std::this_thread::sleep_for(1ms);
		}
		auto image = make_test_image(true);
		tap.Deliver(image);
		image.free();
	});

	auto result = tap.RequestAndWait(2000ms);
	producer.join();

	ASSERT_TRUE(result.has_value());
	EXPECT_FALSE(result->is_flipped_vertically);
	// Bottom row of the flipped source is the real top row
	EXPECT_EQ(result->image_data[0], 0x20);
	EXPECT_EQ(result->image_data[6], 0x10);

	result->free();
}

TEST(RenderedFrameTap, ConcurrentRequestsBothGetFrames)
{
	RenderedFrameTap tap;
	std::atomic<int> received{0};
	std::atomic<bool> stop{false};

	auto requester = [&] {
		auto result = tap.RequestAndWait(5000ms);
		if (result.has_value()) {
			received++;
			result->free();
		}
	};

	std::thread a(requester);
	std::thread b(requester);

	// Feed frames like the present loop does until both are served
	std::thread producer([&] {
		while (!stop) {
			if (tap.IsRequested()) {
				auto image = make_test_image(false);
				tap.Deliver(image);
				image.free();
			}
			std::this_thread::sleep_for(1ms);
		}
	});

	a.join();
	b.join();
	stop = true;
	producer.join();

	EXPECT_EQ(received, 2);
}

} // namespace
