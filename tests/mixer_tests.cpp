// SPDX-FileCopyrightText:  2023-2025 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "audio/mixer.h"

#include <gtest/gtest.h>

static void callback(const uint16_t) {}

constexpr auto ChannelName = "TEST";

namespace {

TEST(MixerConfigureFadeOut, Boolean)
{
	MixerChannel channel(callback, ChannelName, {ChannelFeature::Sleep});

	ASSERT_TRUE(channel.ConfigureFadeOut("on"));
	ASSERT_TRUE(channel.ConfigureFadeOut("off"));
}

TEST(MixerConfigureFadeOut, ShortWait)
{
	MixerChannel channel(callback, ChannelName, {ChannelFeature::Sleep});

	ASSERT_TRUE(channel.ConfigureFadeOut("100 10"));
	ASSERT_TRUE(channel.ConfigureFadeOut("100 1500"));
	ASSERT_TRUE(channel.ConfigureFadeOut("100 3000"));
}

TEST(MixerConfigureFadeOut, MediumWait)
{
	MixerChannel channel(callback, ChannelName, {ChannelFeature::Sleep});

	ASSERT_TRUE(channel.ConfigureFadeOut("2500 10"));
	ASSERT_TRUE(channel.ConfigureFadeOut("2500 1500"));
	ASSERT_TRUE(channel.ConfigureFadeOut("2500 3000"));
}

TEST(MixerConfigureFadeOut, LongWait)
{
	MixerChannel channel(callback, ChannelName, {ChannelFeature::Sleep});

	ASSERT_TRUE(channel.ConfigureFadeOut("5000 10"));
	ASSERT_TRUE(channel.ConfigureFadeOut("5000 1500"));
	ASSERT_TRUE(channel.ConfigureFadeOut("5000 3000"));
}

TEST(MixerConfigureFadeOut, JunkStrings)
{
	MixerChannel channel(callback, ChannelName, {ChannelFeature::Sleep});
	// Junk/invalid
	ASSERT_FALSE(channel.ConfigureFadeOut(""));
	ASSERT_FALSE(channel.ConfigureFadeOut("junk"));
	ASSERT_FALSE(channel.ConfigureFadeOut("a b c"));
}

TEST(MixerConfigureFadeOut, OutOfBounds)
{
	MixerChannel channel(callback, ChannelName, {ChannelFeature::Sleep});
	// Out of bounds
	ASSERT_FALSE(channel.ConfigureFadeOut("99 9"));
	ASSERT_FALSE(channel.ConfigureFadeOut("-1 -10000"));
	ASSERT_FALSE(channel.ConfigureFadeOut("3001 10000"));
}


TEST(MixerDeviceFormat, SampleRateFollowsTheDevice)
{
	EXPECT_EQ(MIXER_PickSampleRate(48000, 44100), 44100);
}

TEST(MixerDeviceFormat, SampleRateFallsBackWhenDeviceReportsNone)
{
	EXPECT_EQ(MIXER_PickSampleRate(48000, 0), 48000);
	EXPECT_EQ(MIXER_PickSampleRate(48000, -1), 48000);
}

TEST(MixerDeviceFormat, BlocksizeFollowsTheDevice)
{
	EXPECT_EQ(MIXER_PickBlocksize(1024, 512), 512);
}

TEST(MixerDeviceFormat, BlocksizeFallsBackWhenDeviceReportsNone)
{
	EXPECT_EQ(MIXER_PickBlocksize(1024, 0), 1024);
	EXPECT_EQ(MIXER_PickBlocksize(1024, -7), 1024);
}

} // namespace
