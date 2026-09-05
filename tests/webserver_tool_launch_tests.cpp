// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "webserver/webserver.h"

#include <gtest/gtest.h>

namespace {

TEST(WebserverToolLaunch, AnyAddressBindsBecomeLoopback)
{
	EXPECT_EQ(WEBSERVER_ToolPageUrl({"0.0.0.0", 8386}, "tools/cheat-workbench.html"),
	          "http://127.0.0.1:8386/tools/cheat-workbench.html");
	EXPECT_EQ(WEBSERVER_ToolPageUrl({"::", 8386}, "tools/cheat-workbench.html"),
	          "http://127.0.0.1:8386/tools/cheat-workbench.html");
}

TEST(WebserverToolLaunch, SpecificAddressIsUsedAsBound)
{
	EXPECT_EQ(WEBSERVER_ToolPageUrl({"127.0.0.1", 8386}, "tools/x.html"),
	          "http://127.0.0.1:8386/tools/x.html");
	EXPECT_EQ(WEBSERVER_ToolPageUrl({"192.168.1.5", 9000}, "tools/x.html"),
	          "http://192.168.1.5:9000/tools/x.html");
}

TEST(WebserverToolLaunch, Ipv6LiteralsGetBrackets)
{
	EXPECT_EQ(WEBSERVER_ToolPageUrl({"::1", 8386}, "tools/x.html"),
	          "http://[::1]:8386/tools/x.html");
}

TEST(WebserverToolLaunch, LoopbackPeerIsKnownPerAddress)
{
	EXPECT_TRUE(WEBSERVER_ToolPageIsLoopback({"0.0.0.0", 1}));
	EXPECT_TRUE(WEBSERVER_ToolPageIsLoopback({"::", 1}));
	EXPECT_TRUE(WEBSERVER_ToolPageIsLoopback({"127.0.0.1", 1}));
	EXPECT_TRUE(WEBSERVER_ToolPageIsLoopback({"::1", 1}));
	EXPECT_FALSE(WEBSERVER_ToolPageIsLoopback({"192.168.1.5", 1}));
}

// The test binary never runs WEBSERVER_Init, so the API is off here.
TEST(WebserverToolLaunch, OpeningWithTheApiOffOpensNothing)
{
	EXPECT_FALSE(WEBSERVER_GetEndpoint().has_value());
	const auto result = WEBSERVER_OpenToolPage(WorkbenchPage, WorkbenchTool);
	EXPECT_EQ(result.outcome, WebserverToolLaunch::ApiOff);
	EXPECT_TRUE(result.url.empty());
}

} // namespace
