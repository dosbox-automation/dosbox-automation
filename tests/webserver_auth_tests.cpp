// SPDX-FileCopyrightText:  2026-2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "webserver/private/auth.h"

#include <string>

#include <gtest/gtest.h>

using Webserver::ConstantTimeEquals;

namespace {

const std::string token =
        "8f3a1c5e9b2d4f6a8c0e2a4c6e8f0a1b"
        "3d5f7a9c1e3b5d7f9a1c3e5b7d9f1a3c";

TEST(WebserverAuth, EqualTokensMatch)
{
	EXPECT_TRUE(ConstantTimeEquals(token, token));
	EXPECT_TRUE(ConstantTimeEquals(std::string(token), std::string(token)));
}

TEST(WebserverAuth, EmptyStringsMatch)
{
	EXPECT_TRUE(ConstantTimeEquals("", ""));
}

// A request without an Authorization header yields an empty candidate
// token; it must never compare equal to the real one.
TEST(WebserverAuth, EmptyCandidateRejected)
{
	EXPECT_FALSE(ConstantTimeEquals("", token));
	EXPECT_FALSE(ConstantTimeEquals(token, ""));
}

TEST(WebserverAuth, DifferentLengthsRejected)
{
	EXPECT_FALSE(ConstantTimeEquals(token, token + "0"));
	EXPECT_FALSE(ConstantTimeEquals(token, token.substr(0, 63)));
}

TEST(WebserverAuth, FirstByteDifferenceRejected)
{
	auto candidate = token;
	candidate[0]   = '0';
	ASSERT_NE(candidate, token);
	EXPECT_FALSE(ConstantTimeEquals(candidate, token));
}

TEST(WebserverAuth, MiddleByteDifferenceRejected)
{
	auto candidate = token;
	candidate[31]  = '0';
	ASSERT_NE(candidate, token);
	EXPECT_FALSE(ConstantTimeEquals(candidate, token));
}

TEST(WebserverAuth, LastByteDifferenceRejected)
{
	auto candidate = token;
	candidate[63]  = '0';
	ASSERT_NE(candidate, token);
	EXPECT_FALSE(ConstantTimeEquals(candidate, token));
}

// All bytes different must reject just like one byte different. The
// constant-time property itself is structural (no early exit in the
// loop); a wall-clock assertion would be flaky, so it is not tested.
TEST(WebserverAuth, CompletelyDifferentRejected)
{
	const std::string candidate(token.size(), 'z');
	EXPECT_FALSE(ConstantTimeEquals(candidate, token));
}

// Embedded null bytes must participate in the comparison, not
// terminate it.
TEST(WebserverAuth, EmbeddedNullBytesCompared)
{
	std::string a = "ab";
	std::string b = "ab";
	a += '\0';
	b += '\0';
	a += "cd";
	b += "ce";
	EXPECT_FALSE(ConstantTimeEquals(a, b));

	auto c = a;
	EXPECT_TRUE(ConstantTimeEquals(a, c));
}

} // namespace
