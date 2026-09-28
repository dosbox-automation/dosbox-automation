// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//
#include "dos/archive/archive_identity.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace {

using namespace ArchiveIdentity;

std::vector<uint8_t> Bytes(const std::string& s)
{
	return {s.begin(), s.end()};
}

// FIPS 180-4 test vectors
TEST(ArchiveIdentity, Sha256MatchesTheStandardVectors)
{
	EXPECT_EQ(Sha256Hex(Bytes("")),
	          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
	EXPECT_EQ(Sha256Hex(Bytes("abc")),
	          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
	EXPECT_EQ(Sha256Hex(Bytes("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")),
	          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(ArchiveIdentity, ListingIsSortedByPathAndEndsWithTheLength)
{
	const auto listing = CanonicalListing(
	        {
	                {"b.txt", 2, 20,  true, 0x0000abcdu},
	                {"a.txt", 1, 10, false,           0}
        },
	        1234);
	EXPECT_EQ(listing, "a.txt\t1\t10\t-\nb.txt\t2\t20\t0000abcd\nlength\t1234\n");
}

TEST(ArchiveIdentity, HashIsOrderIndependent)
{
	const auto one = Hash(
	        {
	                {"a", 1, 1, false, 0},
                        {"b", 2, 2, false, 0}
        },
	        10);
	const auto two = Hash(
	        {
	                {"b", 2, 2, false, 0},
                        {"a", 1, 1, false, 0}
        },
	        10);
	EXPECT_EQ(one, two);
	EXPECT_EQ(one.size(), 64u);
}

TEST(ArchiveIdentity, EveryFieldChangesTheHash)
{
	const auto base = Hash(
	        {
	                {"a", 1, 1, true, 1}
        },
	        10);
	EXPECT_NE(base,
	          Hash(
	                  {
	                          {"b", 1, 1, true, 1}
        },
	                  10));
	EXPECT_NE(base,
	          Hash(
	                  {
	                          {"a", 2, 1, true, 1}
        },
	                  10));
	EXPECT_NE(base,
	          Hash(
	                  {
	                          {"a", 1, 2, true, 1}
        },
	                  10));
	EXPECT_NE(base,
	          Hash(
	                  {
	                          {"a", 1, 1, true, 2}
        },
	                  10));
	EXPECT_NE(base,
	          Hash(
	                  {
	                          {"a", 1, 1, false, 1}
        },
	                  10));
	EXPECT_NE(base,
	          Hash(
	                  {
	                          {"a", 1, 1, true, 1}
        },
	                  11));
}

TEST(ArchiveIdentity, SanitizesTheStem)
{
	EXPECT_EQ(SanitizeStem("Game Title (1992) [v1.1]"),
	          "Game_Title__1992___v1.1_");
	EXPECT_EQ(SanitizeStem("\xc3\xbc"
	                       "bung"),
	          "__bung");
	EXPECT_EQ(SanitizeStem(""), "archive");
	EXPECT_EQ(SanitizeStem("."), "archive");
	EXPECT_EQ(SanitizeStem(".."), "archive");
	EXPECT_EQ(SanitizeStem("ok-name_1.0"), "ok-name_1.0");
}

TEST(ArchiveIdentity, ShortKeyIsStemAndEightHexDigits)
{
	EXPECT_EQ(ShortKey("game", "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"),
	          "game-01234567");
}

} // namespace
