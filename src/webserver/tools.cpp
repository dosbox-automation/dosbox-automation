// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "private/tools.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <set>
#include <unordered_map>

#include "private/auth.h"
#include "utils/checks.h"

CHECK_NARROWING();

namespace Webserver {

bool IsLoopbackPeer(const std::string& addr)
{
	return addr == "127.0.0.1" || addr == "::1" || addr == "::ffff:127.0.0.1";
}

bool IsToolPagePath(const std::string& path)
{
	static const std::set<std::string> tool_pages = {
	        "/tools/cheat-workbench.html",
	};
	return tool_pages.count(path) > 0;
}

static std::string::size_type find_head_tag(const std::string_view html)
{
	constexpr std::string_view tag = "<head";
	auto same_tag                  = [](const char a, const char b) {
                return std::tolower(static_cast<unsigned char>(a)) ==
                       std::tolower(static_cast<unsigned char>(b));
	};
	auto pos = html.begin();
	while (true) {
		pos = std::search(pos, html.end(), tag.begin(), tag.end(), same_tag);
		if (pos == html.end()) {
			return std::string::npos;
		}
		// "<header" is not the head element
		const auto after = pos + tag.size();
		if (after == html.end() || *after == '>' ||
		    std::isspace(static_cast<unsigned char>(*after))) {
			return static_cast<std::string::size_type>(pos -
			                                           html.begin());
		}
		pos = after;
	}
}

std::optional<std::string> InjectToken(const std::string_view html,
                                       const std::string_view token)
{
	if (!IsValidHexToken(std::string(token))) {
		return {};
	}
	const auto head = find_head_tag(html);
	if (head == std::string::npos) {
		return {};
	}
	const auto close = html.find('>', head);
	if (close == std::string::npos) {
		return {};
	}
	std::string out(html);
	out.insert(close + 1,
	           "<script>window.DOSBOX_API_TOKEN=\"" + std::string(token) +
	                   "\";</script>");
	return out;
}

bool IsToolName(const std::string_view tool)
{
	static const std::set<std::string, std::less<>> tool_names = {
	        "cheat-workbench",
	};
	return tool_names.count(tool) > 0;
}

// Written by the webserver thread, read from the emulation thread.
static std::mutex seen_mutex;
static std::unordered_map<std::string, std::chrono::steady_clock::time_point> seen_tools;

void NoteToolPageSeen(const std::string_view tool,
                      const std::chrono::steady_clock::time_point when)
{
	if (!IsToolName(tool)) {
		return;
	}
	const std::lock_guard lock(seen_mutex);
	seen_tools[std::string(tool)] = when;
}

bool ToolPageSeenWithin(const std::string_view tool,
                        const std::chrono::steady_clock::duration window,
                        const std::chrono::steady_clock::time_point now)
{
	const std::lock_guard lock(seen_mutex);
	const auto it = seen_tools.find(std::string(tool));
	if (it == seen_tools.end()) {
		return false;
	}
	return now - it->second <= window;
}

} // namespace Webserver
