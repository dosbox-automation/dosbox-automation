// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "webserver/private/freeze.h"

#include <gtest/gtest.h>

using Webserver::FreezeRegistry;

namespace {

class FreezeTest : public ::testing::Test {
protected:
	FreezeRegistry reg;
};

TEST_F(FreezeTest, AddAndList)
{
	EXPECT_TRUE(reg.Add(0x1000, 99, 1));
	EXPECT_TRUE(reg.Add(0x2000, 5, 2));
	const auto entries = reg.List();
	ASSERT_EQ(entries.size(), 2u);
	EXPECT_EQ(entries[0].address, 0x1000u);
	EXPECT_EQ(entries[0].value, 99u);
	EXPECT_EQ(entries[0].width, 1);
	EXPECT_EQ(entries[1].address, 0x2000u);
	EXPECT_EQ(entries[1].width, 2);
}

TEST_F(FreezeTest, AddSameAddressUpdatesInPlace)
{
	reg.Add(0x1000, 1, 1);
	reg.Add(0x1000, 250, 1);
	ASSERT_EQ(reg.List().size(), 1u);
	EXPECT_EQ(reg.List()[0].value, 250u);
}

TEST_F(FreezeTest, RejectsBeyondCap)
{
	for (int i = 0; i < FreezeRegistry::MaxEntries; ++i) {
		EXPECT_TRUE(reg.Add(0x1000 + i * 4, 0, 1));
	}
	EXPECT_FALSE(reg.Add(0x9000, 0, 1));
}

TEST_F(FreezeTest, RemoveOne)
{
	reg.Add(0x1000, 1, 1);
	reg.Add(0x2000, 2, 1);
	EXPECT_TRUE(reg.Remove(0x1000));
	EXPECT_EQ(reg.List().size(), 1u);
	EXPECT_EQ(reg.List()[0].address, 0x2000u);
}

TEST_F(FreezeTest, RemoveNonexistentReturnsFalse)
{
	EXPECT_FALSE(reg.Remove(0x9999));
}

TEST_F(FreezeTest, ClearAll)
{
	reg.Add(0x1000, 1, 1);
	reg.Add(0x2000, 2, 1);
	reg.Clear();
	EXPECT_TRUE(reg.List().empty());
}

TEST_F(FreezeTest, RejectsBadWidth)
{
	EXPECT_FALSE(reg.Add(0x1000, 0, 3));
	EXPECT_FALSE(reg.Add(0x1000, 0, 0));
}

TEST_F(FreezeTest, AcceptsAllValidWidths)
{
	EXPECT_TRUE(reg.Add(0x1000, 0, 1));
	EXPECT_TRUE(reg.Add(0x2000, 0, 2));
	EXPECT_TRUE(reg.Add(0x3000, 0, 4));
}

} // namespace
