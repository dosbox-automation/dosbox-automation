// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/archive/entry_names.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

using ArchiveNames::DosNameTable;
using ArchiveNames::FoldComponent;
using ArchiveNames::Rejection;
using ArchiveNames::SplitEntryPath;

using Components = std::vector<std::string>;

Rejection RejectionOf(const std::string& path)
{
	auto rejection   = Rejection::None;
	const auto split = SplitEntryPath(path, &rejection);
	EXPECT_FALSE(split.has_value()) << path;
	return rejection;
}

TEST(ArchiveNames, SplitsOnBothSeparators)
{
	const auto split = SplitEntryPath("dir\\sub/file.txt", nullptr);
	ASSERT_TRUE(split.has_value());
	EXPECT_EQ(split->components, (Components{"dir", "sub", "file.txt"}));
	EXPECT_FALSE(split->is_dir);
}

TEST(ArchiveNames, TrailingSeparatorMarksADirectory)
{
	const auto split = SplitEntryPath("dir/sub/", nullptr);
	ASSERT_TRUE(split.has_value());
	EXPECT_EQ(split->components, (Components{"dir", "sub"}));
	EXPECT_TRUE(split->is_dir);
}

TEST(ArchiveNames, TrimsTrailingDotsAndSpacesPerComponent)
{
	const auto split = SplitEntryPath("dir. /file.txt. ", nullptr);
	ASSERT_TRUE(split.has_value());
	EXPECT_EQ(split->components, (Components{"dir", "file.txt"}));
}

TEST(ArchiveNames, RejectsAbsoluteAndDriveLetterPaths)
{
	EXPECT_EQ(RejectionOf("/etc/passwd"), Rejection::AbsolutePath);
	EXPECT_EQ(RejectionOf("\\windows\\x"), Rejection::AbsolutePath);
	EXPECT_EQ(RejectionOf("C:\\x"), Rejection::AbsolutePath);
	EXPECT_EQ(RejectionOf("c:x"), Rejection::AbsolutePath);
}

TEST(ArchiveNames, RejectsParentComponents)
{
	EXPECT_EQ(RejectionOf(".."), Rejection::DotComponent);
	EXPECT_EQ(RejectionOf("a/../b"), Rejection::DotComponent);
	EXPECT_EQ(RejectionOf("../a"), Rejection::DotComponent);
}

// tar -C dir -cf x.tar . names every member ./x and the root itself ./
TEST(ArchiveNames, DropsCurrentDirectoryComponents)
{
	const auto leading = SplitEntryPath("./a/b.txt", nullptr);
	ASSERT_TRUE(leading.has_value());
	EXPECT_EQ(leading->components, (Components{"a", "b.txt"}));
	const auto inner = SplitEntryPath("a/./b.txt", nullptr);
	ASSERT_TRUE(inner.has_value());
	EXPECT_EQ(inner->components, (Components{"a", "b.txt"}));
	const auto root = SplitEntryPath("./", nullptr);
	ASSERT_TRUE(root.has_value());
	EXPECT_TRUE(root->components.empty());
	EXPECT_TRUE(root->is_dir);
	const auto bare = SplitEntryPath(".", nullptr);
	ASSERT_TRUE(bare.has_value());
	EXPECT_TRUE(bare->components.empty());
	EXPECT_TRUE(bare->is_dir);
	const auto trailing = SplitEntryPath("a/.", nullptr);
	ASSERT_TRUE(trailing.has_value());
	EXPECT_EQ(trailing->components, (Components{"a"}));
	EXPECT_TRUE(trailing->is_dir);
	EXPECT_EQ(RejectionOf("./C:/x"), Rejection::AbsolutePath);
	EXPECT_EQ(RejectionOf("./.."), Rejection::DotComponent);
	EXPECT_EQ(RejectionOf(".//a"), Rejection::EmptyComponent);
	EXPECT_EQ(RejectionOf("./CON"), Rejection::ReservedDeviceName);
}

TEST(ArchiveNames, RejectsEmptyComponents)
{
	EXPECT_EQ(RejectionOf(""), Rejection::EmptyComponent);
	EXPECT_EQ(RejectionOf("a//b"), Rejection::EmptyComponent);
	EXPECT_EQ(RejectionOf("a/.../b"), Rejection::EmptyComponent);
	EXPECT_EQ(RejectionOf("   "), Rejection::EmptyComponent);
}

TEST(ArchiveNames, RejectsControlCharactersAndNul)
{
	EXPECT_EQ(RejectionOf("a\x01"
	                      "b"),
	          Rejection::ControlCharacter);
	EXPECT_EQ(RejectionOf(std::string("a\0b", 3)), Rejection::ControlCharacter);
	EXPECT_EQ(RejectionOf("a\x7f"), Rejection::ControlCharacter);
}

TEST(ArchiveNames, RejectsAComponentOver255Bytes)
{
	const auto long_name = std::string(256, 'a');
	EXPECT_EQ(RejectionOf(long_name), Rejection::ComponentTooLong);
	EXPECT_TRUE(SplitEntryPath(std::string(255, 'a'), nullptr).has_value());
}

TEST(ArchiveNames, RejectsReservedDeviceNamesOnEveryHost)
{
	EXPECT_EQ(RejectionOf("CON.TXT"), Rejection::ReservedDeviceName);
	EXPECT_EQ(RejectionOf("aux/x"), Rejection::ReservedDeviceName);
	EXPECT_EQ(RejectionOf("com1 .txt"), Rejection::ReservedDeviceName);
	EXPECT_EQ(RejectionOf("game/Nul"), Rejection::ReservedDeviceName);
	EXPECT_TRUE(SplitEntryPath("console.txt", nullptr).has_value());
}

TEST(ArchiveNames, FoldsAFittingNameToUppercase)
{
	EXPECT_EQ(FoldComponent("game.exe", 1), "GAME.EXE");
	EXPECT_EQ(FoldComponent("README", 1), "README");
}

TEST(ArchiveNames, FoldsALongNameWithATilde)
{
	EXPECT_EQ(FoldComponent("longfilename.text", 1), "LONGFI~1.TEX");
	EXPECT_EQ(FoldComponent("longfilename.text", 12), "LONGF~12.TEX");
}

TEST(ArchiveNames, FoldsNonAsciiToUnderscore)
{
	EXPECT_EQ(FoldComponent("\xc3\xbc"
	                        "bung.txt",
	                        1),
	          "_BUNG.TXT");
	EXPECT_EQ(FoldComponent("\xe2\x82\xac.txt", 1), "_.TXT");
}

// A replaced character is a lossy fold, and FATGEN103 (and the local
// drive's cache) give a lossy name the numeric tail.
TEST(ArchiveNames, FoldsIllegalCharactersToUnderscoreWithATilde)
{
	EXPECT_EQ(FoldComponent("a+b=c.txt", 1), "A_B_C~1.TXT");
}

TEST(ArchiveNames, FoldsANumberAboveOneEvenForAFittingName)
{
	EXPECT_EQ(FoldComponent("abc.txt", 2), "ABC~2.TXT");
}

TEST(ArchiveNames, TableNumbersCollisions)
{
	DosNameTable table = {};
	EXPECT_EQ(table.Assign("", "abc.txt"), "ABC.TXT");
	EXPECT_EQ(table.Assign("", "ABC.txt"), "ABC~2.TXT");
	EXPECT_EQ(table.Assign("", "longfilename1.txt"), "LONGFI~1.TXT");
	EXPECT_EQ(table.Assign("", "longfilename2.txt"), "LONGFI~2.TXT");
	EXPECT_TRUE(table.Contains("ABC~2.TXT"));
	EXPECT_FALSE(table.Contains("ABC~3.TXT"));
}

TEST(ArchiveNames, TableScopesNamesPerDirectory)
{
	DosNameTable table = {};
	EXPECT_EQ(table.Assign("", "abc.txt"), "ABC.TXT");
	EXPECT_EQ(table.Assign("DIR", "abc.txt"), "DIR\\ABC.TXT");
	EXPECT_TRUE(table.Contains("DIR\\ABC.TXT"));
}

TEST(ArchiveNames, TableRefusesAfter999NumberedNames)
{
	DosNameTable table = {};
	for (auto i = 0; i < 999; ++i) {
		ASSERT_TRUE(table.Assign("", "longfilename" + std::to_string(i) + ".txt")
		                    .has_value());
	}
	EXPECT_FALSE(table.Assign("", "longfilename999.txt").has_value());
}

TEST(ArchiveNames, TableRefusesAPathOverDosLength)
{
	DosNameTable table = {};
	auto parent        = std::string();
	for (auto depth = 0; depth < 8; ++depth) {
		const auto assigned = table.Assign(parent, "abcdefgh");
		ASSERT_TRUE(assigned.has_value()) << depth;
		parent = *assigned;
	}
	// 8 levels of "ABCDEFGH" plus separators is 71 characters; DOS_MakeName
	// takes "\\" plus the path under DOS_PATHLENGTH (80), so 78 is the
	// longest: a ninth level of 8 (80) and of 7 (79) refused, of 6 (78) not.
	EXPECT_FALSE(table.Assign(parent, "abcdefgh").has_value());
	EXPECT_TRUE(table.Assign(parent, "abcdef").has_value());
	EXPECT_FALSE(table.Assign(parent, "abcdefg").has_value());
}

TEST(ArchiveNames, FoldsAStrayContinuationByteToOneUnderscore)
{
	EXPECT_EQ(FoldComponent("\x80"
	                        "A.TXT",
	                        1),
	          "_A.TXT");
	EXPECT_EQ(FoldComponent("\xc3", 1), "_");
}

} // namespace
