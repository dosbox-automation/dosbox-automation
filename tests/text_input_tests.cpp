// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "lua/text_input.h"

#include <gtest/gtest.h>

using Lua::CharToKey;
using Lua::KeyStroke;
using Lua::TextToStrokes;

namespace {

TEST(TextInput, LowercaseMapsToUnshiftedQwertyKeys)
{
	EXPECT_EQ(CharToKey('a').key, KBD_a);
	EXPECT_FALSE(CharToKey('a').shift);
	EXPECT_EQ(CharToKey('u').key, KBD_u);
	EXPECT_EQ(CharToKey('z').key, KBD_z);
}

TEST(TextInput, DigitsMapThroughTableNotArithmetic)
{
	EXPECT_EQ(CharToKey('7').key, KBD_7);
	EXPECT_EQ(CharToKey('0').key, KBD_0);
	EXPECT_EQ(CharToKey('3').key, KBD_3);
}

TEST(TextInput, UppercaseSetsShift)
{
	EXPECT_EQ(CharToKey('A').key, KBD_a);
	EXPECT_TRUE(CharToKey('A').shift);
}

TEST(TextInput, ShiftedSymbolsMapToBaseKeyWithShift)
{
	EXPECT_EQ(CharToKey('!').key, KBD_1);
	EXPECT_TRUE(CharToKey('!').shift);
	EXPECT_EQ(CharToKey(':').key, KBD_semicolon);
	EXPECT_TRUE(CharToKey(':').shift);
}

TEST(TextInput, UnmappableCharIsNone)
{
	EXPECT_EQ(CharToKey('\x01').key, KBD_NONE);
}

TEST(TextInput, TextToStrokesSkipsUnmappable)
{
	const auto strokes = TextToStrokes(std::string_view("a\x01z"));
	ASSERT_EQ(strokes.size(), 2u);
	EXPECT_EQ(strokes[0].key, KBD_a);
	EXPECT_EQ(strokes[1].key, KBD_z);
}

TEST(TextInput, AllDigitsMapCorrectly)
{
	const KBD_KEYS expected[] = {KBD_0, KBD_1, KBD_2, KBD_3, KBD_4,
	                             KBD_5, KBD_6, KBD_7, KBD_8, KBD_9};
	for (int d = 0; d <= 9; ++d) {
		const auto stroke = CharToKey(static_cast<char>('0' + d));
		EXPECT_EQ(stroke.key, expected[d])
		        << "digit " << d << " mapped wrong";
		EXPECT_FALSE(stroke.shift);
	}
}

TEST(TextInput, AllLowercaseLettersMapCorrectly)
{
	const KBD_KEYS expected[] = {
	        KBD_a, KBD_b, KBD_c, KBD_d, KBD_e, KBD_f, KBD_g, KBD_h, KBD_i,
	        KBD_j, KBD_k, KBD_l, KBD_m, KBD_n, KBD_o, KBD_p, KBD_q, KBD_r,
	        KBD_s, KBD_t, KBD_u, KBD_v, KBD_w, KBD_x, KBD_y, KBD_z,
	};
	for (int i = 0; i < 26; ++i) {
		const auto stroke = CharToKey(static_cast<char>('a' + i));
		EXPECT_EQ(stroke.key, expected[i])
		        << "letter '" << static_cast<char>('a' + i) << "' mapped wrong";
		EXPECT_FALSE(stroke.shift);
	}
}

} // namespace
