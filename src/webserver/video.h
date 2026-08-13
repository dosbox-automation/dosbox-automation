// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_WEBSERVER_VIDEO_H
#define DOSBOX_WEBSERVER_VIDEO_H

#include "libs/http/http.h"

#include "webserver/bridge.h"

#include <string>

namespace Webserver {

struct VideoHandlers {
	static void GetFrame(const httplib::Request& req, httplib::Response& res);
	static void GetFrameInfo(const httplib::Request& req, httplib::Response& res);
};

// Text-mode character buffer read (emulation thread via Bridge).
class ScreenTextCommand : public Command {
	bool is_text_mode = false;
	int columns       = 0;
	int rows          = 0;
	int page          = 0;
	int bios_mode     = 0;
	std::string text_dos = {};

public:
	void Execute() override;
	static void Get(const httplib::Request& req, httplib::Response& res);
};

} // namespace Webserver

#endif // DOSBOX_WEBSERVER_VIDEO_H
