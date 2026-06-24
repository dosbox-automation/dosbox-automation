// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_WEBSERVER_VIDEO_H
#define DOSBOX_WEBSERVER_VIDEO_H

#include "libs/http/http.h"

namespace Webserver {

struct VideoHandlers {
	static void GetFrame(const httplib::Request& req, httplib::Response& res);
	static void GetFrameInfo(const httplib::Request& req, httplib::Response& res);
};

} // namespace Webserver

#endif // DOSBOX_WEBSERVER_VIDEO_H
