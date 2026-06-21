// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_WEBSERVER_CAPTURE_H
#define DOSBOX_WEBSERVER_CAPTURE_H

#include "webserver/bridge.h"
#include "libs/http/http.h"

namespace Webserver {

class CaptureStartCommand : public Command {
public:
	void Execute() override;
	static void Post(const httplib::Request& req, httplib::Response& res);
};

class CaptureStopCommand : public Command {
public:
	void Execute() override;
	static void Post(const httplib::Request& req, httplib::Response& res);
};

class CaptureStatusCommand : public Command {
public:
	void Execute() override;
	static void Get(const httplib::Request& req, httplib::Response& res);

	bool capturing = false;
};

} // namespace Webserver

#endif // DOSBOX_WEBSERVER_CAPTURE_H
