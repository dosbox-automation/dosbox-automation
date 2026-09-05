// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_WEBSERVER_TOOLS_H
#define DOSBOX_WEBSERVER_TOOLS_H

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

// Tool pages are served outside the static mounts so the token can be
// written into the document for a browser on this machine. The token
// is only ever delivered inside HTML, never in a response a browser
// would execute cross-origin (ada-fb7c).
namespace Webserver {

bool IsLoopbackPeer(const std::string& addr);

// Exact match against the registered tool pages, same reasoning as
// IsPublicDocPath: no prefix or normalised match.
bool IsToolPagePath(const std::string& path);

// Inserts the token script right after the first <head> tag. Empty
// when the page has no head or the token is not 64 hex characters.
std::optional<std::string> InjectToken(std::string_view html, std::string_view token);

// A tool page marks its own API calls with the X-DOSBox-Tool header
// so the engine can tell a live page from none. Only registered names
// are noted; the clock is a parameter so tests need no waiting.
bool IsToolName(std::string_view tool);
void NoteToolPageSeen(std::string_view tool,
                      std::chrono::steady_clock::time_point when);
bool ToolPageSeenWithin(std::string_view tool,
                        std::chrono::steady_clock::duration window,
                        std::chrono::steady_clock::time_point now);

} // namespace Webserver

#endif // DOSBOX_WEBSERVER_TOOLS_H
