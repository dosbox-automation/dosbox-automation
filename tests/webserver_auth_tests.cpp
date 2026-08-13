// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "webserver/private/auth.h"
#include "webserver/webserver.h"

#include <string>

#include <gtest/gtest.h>

using Webserver::ConstantTimeEquals;
using Webserver::IsPublicDocPath;

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

// -- Documentation path allowlist (token bypass) --

TEST(WebserverDocPath, AllowsDocAssetsForGet)
{
	for (const auto* p : {"/",
	                      "/index.html",
	                      "/style-index.css",
	                      "/api.html",
	                      "/openapi.json",
	                      "/swagger-ui.css",
	                      "/swagger-ui-bundle.js"}) {
		EXPECT_TRUE(IsPublicDocPath("GET", p)) << p;
		EXPECT_TRUE(IsPublicDocPath("HEAD", p)) << p;
	}
}

TEST(WebserverDocPath, RejectsNonReadMethods)
{
	EXPECT_FALSE(IsPublicDocPath("POST", "/openapi.json"));
	EXPECT_FALSE(IsPublicDocPath("PUT", "/index.html"));
	EXPECT_FALSE(IsPublicDocPath("DELETE", "/"));
	EXPECT_FALSE(IsPublicDocPath("OPTIONS", "/api.html"));
}

TEST(WebserverDocPath, NeverExposesTokenFile)
{
	EXPECT_FALSE(IsPublicDocPath("GET", "/api_token"));
	EXPECT_FALSE(IsPublicDocPath("GET", "/.dosbox/api_token"));
}

TEST(WebserverDocPath, RejectsApiEndpoints)
{
	EXPECT_FALSE(IsPublicDocPath("GET", "/api/v1/status"));
	EXPECT_FALSE(IsPublicDocPath("GET", "/api/v1/cpu/state"));
}

TEST(WebserverDocPath, RejectsTraversalAndTrickyVariants)
{
	// httplib hands us the already-decoded path; an exact-match allowlist
	// fails closed on anything that is not byte-for-byte a known asset.
	EXPECT_FALSE(IsPublicDocPath("GET", "/openapi.json/../api_token"));
	EXPECT_FALSE(IsPublicDocPath("GET", "/../api_token"));
	EXPECT_FALSE(IsPublicDocPath("GET", "/./index.html"));
	EXPECT_FALSE(IsPublicDocPath("GET", "//index.html"));
	EXPECT_FALSE(IsPublicDocPath("GET", "/index.html "));
	EXPECT_FALSE(IsPublicDocPath("GET", "/INDEX.HTML"));
	EXPECT_FALSE(IsPublicDocPath("GET", "/openapi.json?x=1"));
	EXPECT_FALSE(IsPublicDocPath("GET", ""));
}

} // namespace
