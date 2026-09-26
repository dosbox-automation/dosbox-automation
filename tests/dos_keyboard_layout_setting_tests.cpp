// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "dos/dos_locale.h"

#include <gtest/gtest.h>

#include <string>

#include "config/setup.h"
#include "dosbox_test_fixture.h"
#include "misc/host_locale.h"

namespace {

TEST(KeyboardLayoutSetting, EmptyMeansTheDefaultLayout)
{
	const auto setting = DOS_ParseKeyboardLayoutSetting("");

	EXPECT_EQ(setting.layout, "us");
	EXPECT_FALSE(setting.code_page);
	EXPECT_FALSE(setting.is_auto);
	EXPECT_FALSE(setting.is_invalid);
}

// ada-b9e0: a host guess acted on silently cost us twice, so 'auto'
// only keeps old configs valid
TEST(KeyboardLayoutSetting, AutoMeansTheDefaultLayoutAndIsReported)
{
	const auto setting = DOS_ParseKeyboardLayoutSetting("auto");

	EXPECT_EQ(setting.layout, "us");
	EXPECT_FALSE(setting.code_page);
	EXPECT_TRUE(setting.is_auto);
}

TEST(KeyboardLayoutSetting, LayoutAlone)
{
	const auto setting = DOS_ParseKeyboardLayoutSetting("gr");

	EXPECT_EQ(setting.layout, "gr");
	EXPECT_FALSE(setting.code_page);
	EXPECT_FALSE(setting.is_auto);
}

TEST(KeyboardLayoutSetting, LayoutWithCodePage)
{
	const auto setting = DOS_ParseKeyboardLayoutSetting("gr 850");

	EXPECT_EQ(setting.layout, "gr");
	ASSERT_TRUE(setting.code_page);
	EXPECT_EQ(*setting.code_page, 850);
	EXPECT_FALSE(setting.is_code_page_invalid);
}

TEST(KeyboardLayoutSetting, UnusableCodePageIsDroppedAndReported)
{
	for (const auto* value : {"gr abc", "gr 0", "gr 65536", "gr -1"}) {
		SCOPED_TRACE(value);
		const auto setting = DOS_ParseKeyboardLayoutSetting(value);

		EXPECT_EQ(setting.layout, "gr");
		EXPECT_FALSE(setting.code_page);
		EXPECT_TRUE(setting.is_code_page_invalid);
	}
}

TEST(KeyboardLayoutSetting, TooManyWordsFallBackToTheDefault)
{
	const auto setting = DOS_ParseKeyboardLayoutSetting("gr 850 extra");

	EXPECT_EQ(setting.layout, "us");
	EXPECT_FALSE(setting.code_page);
	EXPECT_TRUE(setting.is_invalid);
}

using Kind    = KeyboardLayoutHint::Kind;
using Layouts = std::vector<KeyboardLayoutMaybeCodepage>;

TEST(KeyboardLayoutHint, NamesTheHostLayoutWhenTheDefaultIsInUse)
{
	const auto hint = DOS_GetKeyboardLayoutHint("us", Layouts{{"gr"}, {"us"}}, {});

	ASSERT_TRUE(hint);
	EXPECT_EQ(hint->kind, Kind::SwitchTo);
	EXPECT_EQ(hint->layout, "gr");
	EXPECT_FALSE(hint->code_page);
}

TEST(KeyboardLayoutHint, SilentWhenTheHostUsesTheSameLayout)
{
	EXPECT_FALSE(DOS_GetKeyboardLayoutHint("us", Layouts{{"us"}, {"gr"}}, {}));
}

// D6: only the default gets the hint; any other layout was chosen
TEST(KeyboardLayoutHint, SilentWhenAnotherLayoutIsInUse)
{
	EXPECT_FALSE(DOS_GetKeyboardLayoutHint("fr", Layouts{{"gr"}}, {}));
	EXPECT_FALSE(DOS_GetKeyboardLayoutHint("fr", {}, {"custom"}));
}

// A written 'us' gets the hint like the default, by decision (design D6,
// ada-2lbz closed); pinned so a change here is deliberate
TEST(KeyboardLayoutHint, AWrittenUsStillGetsTheHint)
{
	const auto hint = DOS_GetKeyboardLayoutHint("us", Layouts{{"gr"}}, {});

	ASSERT_TRUE(hint);
	EXPECT_EQ(hint->layout, "gr");
}

TEST(KeyboardLayoutHint, SaysSoWhenTheHostLayoutHasNoDosEquivalent)
{
	const auto hint = DOS_GetKeyboardLayoutHint("us", {}, {"custom", "foo"});

	ASSERT_TRUE(hint);
	EXPECT_EQ(hint->kind, Kind::NoMapping);
	EXPECT_EQ(hint->layout, "custom");
}

// A mapped host layout always wins over an unmapped one
TEST(KeyboardLayoutHint, PrefersAMappedLayoutOverAnUnmappedOne)
{
	const auto hint = DOS_GetKeyboardLayoutHint("us", Layouts{{"gr"}}, {"custom"});

	ASSERT_TRUE(hint);
	EXPECT_EQ(hint->kind, Kind::SwitchTo);
	EXPECT_EQ(hint->layout, "gr");
}

// The host tables mark a poor mapping as fuzzy; "looks French" for N'Ko
// would be wrong, so such an entry is passed over
TEST(KeyboardLayoutHint, SkipsAFuzzyMapping)
{
	const auto fuzzy = KeyboardLayoutMaybeCodepage{"fr", {}, true};

	EXPECT_FALSE(DOS_GetKeyboardLayoutHint("us", Layouts{fuzzy}, {}));

	const auto hint = DOS_GetKeyboardLayoutHint("us", Layouts{fuzzy, {"gr"}}, {});
	ASSERT_TRUE(hint);
	EXPECT_EQ(hint->layout, "gr");
}

TEST(KeyboardLayoutHint, CarriesTheCodePageOfTheMapping)
{
	const auto hint = DOS_GetKeyboardLayoutHint("us",
	                                            Layouts{
	                                                    {"gr453", 852}
        },
	                                            {});

	ASSERT_TRUE(hint);
	EXPECT_EQ(hint->layout, "gr453");
	ASSERT_TRUE(hint->code_page);
	EXPECT_EQ(*hint->code_page, 852);
}

// The name comes from the desktop's own config files and goes to the DOS
// console: no escape sequences, no UTF-8, no runaway length
TEST(KeyboardLayoutHint, HostNameIsPrintableAsciiAndBounded)
{
	const auto hostile = std::string("\x1b[31mcus\xc3\xa4tom") +
	                     std::string(100, 'x');

	const auto hint = DOS_GetKeyboardLayoutHint("us", {}, {hostile});

	ASSERT_TRUE(hint);
	EXPECT_LE(hint->layout.size(), 32u);
	for (const auto c : hint->layout) {
		const auto byte = static_cast<unsigned char>(c);
		EXPECT_TRUE(byte >= 0x20 && byte <= 0x7e) << static_cast<int>(byte);
	}
	EXPECT_NE(hint->layout.find("cus??tom"), std::string::npos) << hint->layout;
}

TEST(KeyboardLayoutHint, SilentWhenNothingWasDetected)
{
	EXPECT_FALSE(DOS_GetKeyboardLayoutHint("us", {}, {}));
}

class KeyboardLayoutSettingDefault : public DOSBoxTestFixture {};

TEST_F(KeyboardLayoutSettingDefault, RegisteredDefaultIsUs)
{
	const auto section = get_section("dos");
	ASSERT_NE(section, nullptr);
	const auto property = section->GetProperty("keyboard_layout");
	ASSERT_NE(property, nullptr);

	EXPECT_EQ(property->GetDefaultValue().ToString(), "us");
}

} // namespace
