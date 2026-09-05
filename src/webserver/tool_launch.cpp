// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "private/tools.h"
#include "webserver.h"

#include <chrono>
#include <string>
#include <string_view>

#include <SDL3/SDL.h>

#include "augra/log.h"
#include "gui/osd/osd.h"
#include "gui/private/common.h"
#include "utils/checks.h"

CHECK_NARROWING();

static bool is_any_address(const std::string& addr)
{
	return addr == "0.0.0.0" || addr == "::";
}

bool WEBSERVER_ToolPageIsLoopback(const WebserverEndpoint& endpoint)
{
	const auto& addr = endpoint.bind_address;
	return is_any_address(addr) || addr == "127.0.0.1" || addr == "::1";
}

std::string WEBSERVER_ToolPageUrl(const WebserverEndpoint& endpoint,
                                  const std::string_view page)
{
	std::string host = is_any_address(endpoint.bind_address)
	                         ? "127.0.0.1"
	                         : endpoint.bind_address;
	if (host.find(':') != std::string::npos) {
		host = "[" + host + "]";
	}
	return "http://" + host + ":" + std::to_string(endpoint.port) + "/" +
	       std::string(page);
}

WebserverToolLaunchResult WEBSERVER_OpenToolPage(const std::string_view page,
                                                 const std::string_view tool)
{
	using namespace std::chrono_literals;

	const auto endpoint = WEBSERVER_GetEndpoint();
	if (!endpoint) {
		return {WebserverToolLaunch::ApiOff, {}};
	}
	const auto url = WEBSERVER_ToolPageUrl(*endpoint, page);
	// Five seconds covers a page polling every 250 ms, and a hidden tab
	// throttled to one call a second, without refusing for long after
	// the tab was closed.
	if (Webserver::ToolPageSeenWithin(tool, 5s, std::chrono::steady_clock::now())) {
		return {WebserverToolLaunch::AlreadyOpen, url};
	}
	if (!SDL_OpenURL(url.c_str())) {
		augra::log_warn("webserver",
		                "could not open %s: %s",
		                url.c_str(),
		                SDL_GetError());
		return {WebserverToolLaunch::BrowserFailed, url};
	}
	augra::log_info("webserver", "opened %s in the browser", url.c_str());
	return {WebserverToolLaunch::Opened, url};
}

static void show_workbench_osd(const std::string& text)
{
	OSD::TextOverlay overlay;
	overlay.text     = text;
	overlay.position = OSD::Position::TopLeft;
	overlay.expire_frame = static_cast<int64_t>(GFX_GetRenderedFrameCount()) + 180;
	overlay.tag = "workbench";
	auto& osd   = OSD::OsdManager::Instance();
	osd.ClearByTag("workbench");
	osd.ShowText(std::move(overlay));
}

void WEBSERVER_WorkbenchHotkey(const bool pressed)
{
	if (!pressed) {
		return;
	}
	const auto result = WEBSERVER_OpenToolPage(WorkbenchPage, WorkbenchTool);
	switch (result.outcome) {
	case WebserverToolLaunch::Opened:
		show_workbench_osd("Cheat Workbench opened in the browser");
		break;
	case WebserverToolLaunch::AlreadyOpen:
		show_workbench_osd("Cheat Workbench is already open in your browser");
		break;
	case WebserverToolLaunch::ApiOff:
		show_workbench_osd("Web API is off: set webserver_enabled = true");
		break;
	case WebserverToolLaunch::BrowserFailed:
		show_workbench_osd("No browser; open " + result.url);
		break;
	}
}
