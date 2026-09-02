// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "capture/video/zmbv.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace {

constexpr int Width  = 32;
constexpr int Height = 8;

constexpr int PaletteEntries = 256;

// One tag byte, then the six-byte keyframe header (version pair,
// compression, format, block width, block height).
constexpr int KeyframeTagAndHeaderBytes = 1 + 6;

// A keyframe's deflate payload is the palette as RGB triplets followed
// by the raw pixel rows.
constexpr size_t KeyframePayloadBytes = PaletteEntries * 3 + Width * Height;

using Palette = std::array<uint8_t, PaletteEntries * 4>;

std::vector<uint8_t> MakeFrame(const uint8_t seed)
{
	std::vector<uint8_t> frame(Width * Height);
	for (int y = 0; y < Height; ++y) {
		for (int x = 0; x < Width; ++x) {
			frame[y * Width + x] = static_cast<uint8_t>(
			        (x + y * 3 + seed) & 0xff);
		}
	}
	return frame;
}

Palette MakePalette()
{
	Palette palette = {};
	for (int i = 0; i < PaletteEntries; ++i) {
		palette[i * 4 + 0] = static_cast<uint8_t>(i);
		palette[i * 4 + 1] = static_cast<uint8_t>(255 - i);
		palette[i * 4 + 2] = static_cast<uint8_t>(i / 2);
	}
	return palette;
}

int CompressFrame(VideoCodec& encoder, const std::vector<uint8_t>& frame,
                  const int flags, const Palette& palette, std::vector<uint8_t>& out)
{
	if (!encoder.PrepareCompressFrame(flags,
	                                  ZMBV_FORMAT::BPP_8,
	                                  palette.data(),
	                                  out.data(),
	                                  static_cast<uint32_t>(out.size()))) {
		return -1;
	}
	for (int y = 0; y < Height; ++y) {
		const uint8_t* row = frame.data() + y * Width;
		encoder.CompressLines(1, &row);
	}
	return encoder.FinishCompressFrame();
}

// Reads the encoder's output the way an AVI player does: every frame is
// sync-flushed into one continuing deflate stream that a keyframe resets,
// so inflate is asked for progress, never for an end marker.
class StreamReader {
public:
	StreamReader()
	{
		EXPECT_EQ(inflateInit(&stream), Z_OK);
	}
	~StreamReader()
	{
		inflateEnd(&stream);
	}
	StreamReader(const StreamReader&)            = delete;
	StreamReader& operator=(const StreamReader&) = delete;

	// Inflates one frame's payload after `skip` header bytes and returns
	// exactly what came out.
	std::vector<uint8_t> Read(const std::vector<uint8_t>& frame_bytes,
	                          const int count, const size_t skip,
	                          const size_t capacity)
	{
		std::vector<uint8_t> in(frame_bytes.begin() + static_cast<long>(skip),
		                        frame_bytes.begin() + count);
		std::vector<uint8_t> raw(capacity);

		stream.next_in   = in.data();
		stream.avail_in  = static_cast<uint32_t>(in.size());
		stream.next_out  = raw.data();
		stream.avail_out = static_cast<uint32_t>(raw.size());
		stream.total_out = 0;

		EXPECT_EQ(inflate(&stream, Z_SYNC_FLUSH), Z_OK);
		EXPECT_EQ(stream.avail_in, 0u);
		raw.resize(stream.total_out);
		return raw;
	}

	void Reset()
	{
		EXPECT_EQ(inflateReset(&stream), Z_OK);
	}

private:
	z_stream stream = {};
};

void ExpectKeyframeHeader(const std::vector<uint8_t>& out)
{
	EXPECT_EQ(out[0] & 1, 1) << "keyframe tag bit";
	EXPECT_EQ(out[1], 0) << "high version";
	EXPECT_EQ(out[2], 1) << "low version";
	EXPECT_EQ(out[3], 1) << "compression = zlib";
	EXPECT_EQ(out[4], static_cast<uint8_t>(ZMBV_FORMAT::BPP_8));
	EXPECT_EQ(out[5], 16) << "block width";
	EXPECT_EQ(out[6], 16) << "block height";
}

void ExpectKeyframePayload(const std::vector<uint8_t>& raw,
                           const std::vector<uint8_t>& frame, const Palette& palette)
{
	ASSERT_EQ(raw.size(), KeyframePayloadBytes);
	for (int i = 0; i < PaletteEntries; ++i) {
		EXPECT_EQ(raw[i * 3 + 0], palette[i * 4 + 0])
		        << "palette entry " << i;
		EXPECT_EQ(raw[i * 3 + 1], palette[i * 4 + 1])
		        << "palette entry " << i;
		EXPECT_EQ(raw[i * 3 + 2], palette[i * 4 + 2])
		        << "palette entry " << i;
	}
	const std::vector<uint8_t> pixels(raw.begin() + PaletteEntries * 3,
	                                  raw.end());
	EXPECT_EQ(pixels, frame);
}

TEST(ZmbvCodec, DeflateLibraryNamesItselfWithAVersion)
{
	const auto library = ZMBV_DeflateLibrary();
	EXPECT_TRUE(library.starts_with("zlib")) << library;
	EXPECT_NE(library.find('.'), std::string::npos) << library;
}

TEST(ZmbvCodec, KeyframeInflatesToPaletteAndPixels)
{
	const auto palette = MakePalette();
	const auto frame   = MakeFrame(0);

	VideoCodec encoder;
	ASSERT_TRUE(encoder.SetupCompress(Width, Height, 6));
	std::vector<uint8_t> out(
	        encoder.NeededSize(Width, Height, ZMBV_FORMAT::BPP_8));

	const auto bytes = CompressFrame(encoder, frame, 1, palette, out);
	ASSERT_GT(bytes, KeyframeTagAndHeaderBytes);
	ExpectKeyframeHeader(out);

	StreamReader reader;
	const auto raw = reader.Read(out,
	                             bytes,
	                             KeyframeTagAndHeaderBytes,
	                             KeyframePayloadBytes);
	ExpectKeyframePayload(raw, frame, palette);

	encoder.FinishVideo();
}

TEST(ZmbvCodec, DeltaFrameContinuesTheStreamAndAKeyframeRestartsIt)
{
	const auto palette = MakePalette();
	const auto first   = MakeFrame(0);
	const auto second  = MakeFrame(7);

	VideoCodec encoder;
	ASSERT_TRUE(encoder.SetupCompress(Width, Height, 6));
	std::vector<uint8_t> out(
	        encoder.NeededSize(Width, Height, ZMBV_FORMAT::BPP_8));

	StreamReader reader;

	auto bytes = CompressFrame(encoder, first, 1, palette, out);
	ASSERT_GT(bytes, KeyframeTagAndHeaderBytes);
	reader.Read(out, bytes, KeyframeTagAndHeaderBytes, KeyframePayloadBytes);

	// A delta frame has only a tag byte in front of its payload, and the
	// payload (block vectors plus XOR residue) continues the same stream.
	bytes = CompressFrame(encoder, second, 0, palette, out);
	ASSERT_GT(bytes, 1);
	EXPECT_EQ(out[0] & 1, 0) << "delta frames carry no keyframe bit";
	const auto delta = reader.Read(out, bytes, 1, KeyframePayloadBytes);
	EXPECT_GT(delta.size(), 0u);

	bytes = CompressFrame(encoder, first, 1, palette, out);
	ASSERT_GT(bytes, KeyframeTagAndHeaderBytes);
	reader.Reset();
	const auto raw = reader.Read(out,
	                             bytes,
	                             KeyframeTagAndHeaderBytes,
	                             KeyframePayloadBytes);
	ExpectKeyframePayload(raw, first, palette);

	encoder.FinishVideo();
}

TEST(ZmbvCodec, EveryDeflateLevelProducesAReadableKeyframe)
{
	const auto palette = MakePalette();
	const auto frame   = MakeFrame(3);

	for (int level = 0; level <= 9; ++level) {
		VideoCodec encoder;
		ASSERT_TRUE(encoder.SetupCompress(Width, Height, level))
		        << "level " << level;
		std::vector<uint8_t> out(
		        encoder.NeededSize(Width, Height, ZMBV_FORMAT::BPP_8));

		const auto bytes = CompressFrame(encoder, frame, 1, palette, out);
		ASSERT_GT(bytes, KeyframeTagAndHeaderBytes) << "level " << level;

		StreamReader reader;
		const auto raw = reader.Read(out,
		                             bytes,
		                             KeyframeTagAndHeaderBytes,
		                             KeyframePayloadBytes);
		ExpectKeyframePayload(raw, frame, palette);

		encoder.FinishVideo();
	}
}

TEST(ZmbvCodec, NeededSizeCoversAStoredKeyframe)
{
	const auto palette = MakePalette();
	const auto frame   = MakeFrame(11);

	VideoCodec encoder;
	ASSERT_TRUE(encoder.SetupCompress(Width, Height, 0));
	const auto needed = encoder.NeededSize(Width, Height, ZMBV_FORMAT::BPP_8);
	ASSERT_GT(needed, 0);
	std::vector<uint8_t> out(static_cast<size_t>(needed));

	// Level 0 stores instead of compressing, the largest a frame can get;
	// the encoder must never report more bytes than the buffer holds.
	const auto bytes = CompressFrame(encoder, frame, 1, palette, out);
	ASSERT_GT(bytes, KeyframeTagAndHeaderBytes);
	EXPECT_LE(bytes, needed);

	encoder.FinishVideo();
}

} // namespace
