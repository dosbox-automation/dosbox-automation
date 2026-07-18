// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_WEBSERVER_CONTROL_H
#define DOSBOX_WEBSERVER_CONTROL_H

#include "private/shutdown.h"

#include "http/http.h"

namespace Webserver {

struct ControlHandlers {
	static void GetProgramState(const httplib::Request& req, httplib::Response& res);
	static void GetStatus(const httplib::Request& req, httplib::Response& res);
};

} // namespace Webserver

#endif // DOSBOX_WEBSERVER_CONTROL_H
