// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "webserver/private/tools.h"

#include <chrono>
#include <string>

#include <gtest/gtest.h>

using Webserver::InjectToken;
using Webserver::IsLoopbackPeer;
using Webserver::IsToolPagePath;
using Webserver::NoteToolPageSeen;
using Webserver::ToolPageSeenWithin;

using namespace std::chrono_literals;

namespace {

const std::string token =
        "8f3a1c5e9b2d4f6a8c0e2a4c6e8f0a1b"
        "3d5f7a9c1e3b5d7f9a1c3e5b7d9f1a3c";

TEST(WebserverTools, LoopbackPeersAreRecognised)
{
	EXPECT_TRUE(IsLoopbackPeer("127.0.0.1"));
	EXPECT_TRUE(IsLoopbackPeer("::1"));
	EXPECT_TRUE(IsLoopbackPeer("::ffff:127.0.0.1"));
}

TEST(WebserverTools, EverythingElseIsNotLoopback)
{
	EXPECT_FALSE(IsLoopbackPeer(""));
	EXPECT_FALSE(IsLoopbackPeer("127.0.0.2"));
	EXPECT_FALSE(IsLoopbackPeer("192.168.1.5"));
	EXPECT_FALSE(IsLoopbackPeer("localhost"));
	EXPECT_FALSE(IsLoopbackPeer("::2"));
	EXPECT_FALSE(IsLoopbackPeer("127.0.0.1 "));
}

TEST(WebserverTools, TokenLandsRightAfterHead)
{
	const auto out = InjectToken("<html><head><title>x</title></head>", token);
	ASSERT_TRUE(out.has_value());
	EXPECT_EQ(*out,
	          "<html><head><script>window.DOSBOX_API_TOKEN=\"" + token +
	                  "\";</script><title>x</title></head>");
}

TEST(WebserverTools, HeadWithAttributesAndMixedCaseIsFound)
{
	const auto out = InjectToken("<HEAD lang=\"en\"><body>", token);
	ASSERT_TRUE(out.has_value());
	EXPECT_NE(out->find("<script>window.DOSBOX_API_TOKEN"), std::string::npos);
	EXPECT_EQ(out->find("<script>"), std::string("<HEAD lang=\"en\">").size());
}

TEST(WebserverTools, InjectedExactlyOnce)
{
	const auto out = InjectToken("<head></head><head></head>", token);
	ASSERT_TRUE(out.has_value());
	size_t n = 0;
	for (size_t p = out->find(token); p != std::string::npos;
	     p        = out->find(token, p + 1)) {
		++n;
	}
	EXPECT_EQ(n, 1u);
}

TEST(WebserverTools, NoHeadMeansNoInjection)
{
	EXPECT_FALSE(
	        InjectToken("<html><body>no head</body></html>", token).has_value());
}

TEST(WebserverTools, BadTokenIsRefused)
{
	EXPECT_FALSE(InjectToken("<head></head>", "").has_value());
	EXPECT_FALSE(InjectToken("<head></head>", token.substr(0, 63)).has_value());
	EXPECT_FALSE(InjectToken("<head></head>",
	                         "\"><script>alert(1)</script>" + token.substr(27))
	                     .has_value());
}

TEST(WebserverTools, OnlyRegisteredToolPathsMatch)
{
	EXPECT_TRUE(IsToolPagePath("/tools/cheat-workbench.html"));
	EXPECT_FALSE(IsToolPagePath("/tools/../api_token"));
	EXPECT_FALSE(IsToolPagePath("/tools/cheat-workbench.html/"));
	EXPECT_FALSE(IsToolPagePath("/TOOLS/cheat-workbench.html"));
	EXPECT_FALSE(IsToolPagePath("/cheat-workbench.html"));
	EXPECT_FALSE(IsToolPagePath(""));
}

TEST(WebserverTools, ToolPageIsUnseenUntilNoted)
{
	const auto now = std::chrono::steady_clock::now();
	EXPECT_FALSE(ToolPageSeenWithin("never-noted", 5s, now));
}

TEST(WebserverTools, ToolPageSeenInsideTheWindow)
{
	const auto now = std::chrono::steady_clock::now();
	NoteToolPageSeen("cheat-workbench", now - 4s);
	EXPECT_TRUE(ToolPageSeenWithin("cheat-workbench", 5s, now));
}

TEST(WebserverTools, ToolPageForgottenPastTheWindow)
{
	const auto now = std::chrono::steady_clock::now();
	NoteToolPageSeen("cheat-workbench", now - 6s);
	EXPECT_FALSE(ToolPageSeenWithin("cheat-workbench", 5s, now));
}

TEST(WebserverTools, UnregisteredToolNamesAreNotNoted)
{
	const auto now = std::chrono::steady_clock::now();
	NoteToolPageSeen("../api_token", now);
	NoteToolPageSeen("", now);
	EXPECT_FALSE(ToolPageSeenWithin("../api_token", 5s, now));
	EXPECT_FALSE(ToolPageSeenWithin("", 5s, now));
}

} // namespace
