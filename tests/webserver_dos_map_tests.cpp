// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "webserver/private/dos.h"

#include <gtest/gtest.h>

using Webserver::McbBlock;
using Webserver::WalkMcbChain;

namespace {

TEST(DosMap, WalksToEndingBlock)
{
	std::vector<McbBlock> synthetic = {
	        {0x0100, 0x4D, 0x0008, 0x000B, "COMMAND", false},
	        {0x010C, 0x5A, 0x0000, 0x0010, "", false},
	};
	size_t idx = 0;
	auto reader = [&](uint16_t) -> McbBlock {
		return synthetic[idx++];
	};
	auto chain = WalkMcbChain(0x0100, reader, 100);
	ASSERT_EQ(chain.size(), 2u);
	EXPECT_EQ(chain[0].psp_segment, 0x0008u);
	EXPECT_EQ(chain[0].filename, "COMMAND");
	EXPECT_FALSE(chain[0].is_last);
	EXPECT_TRUE(chain[1].is_last);
}

TEST(DosMap, StopsAtMaxBlocksOnCorruptChain)
{
	auto reader = [&](uint16_t) -> McbBlock {
		return {0x0100, 0x4D, 0x0008, 0x0001, "", false};
	};
	auto chain = WalkMcbChain(0x0100, reader, 10);
	EXPECT_EQ(chain.size(), 10u);
}

TEST(DosMap, StopsOnInvalidType)
{
	std::vector<McbBlock> synthetic = {
	        {0x0100, 0x4D, 0x0008, 0x000B, "", false},
	        {0x010C, 0x00, 0x0000, 0x0010, "", false},
	};
	size_t idx = 0;
	auto reader = [&](uint16_t) -> McbBlock {
		return synthetic[idx++];
	};
	auto chain = WalkMcbChain(0x0100, reader, 100);
	EXPECT_EQ(chain.size(), 1u);
}

TEST(DosMap, SingleEndingBlock)
{
	auto reader = [&](uint16_t) -> McbBlock {
		return {0x0100, 0x5A, 0x0000, 0x0010, "FREE", false};
	};
	auto chain = WalkMcbChain(0x0100, reader, 100);
	ASSERT_EQ(chain.size(), 1u);
	EXPECT_TRUE(chain[0].is_last);
	EXPECT_EQ(chain[0].filename, "FREE");
}

} // namespace
