// SPDX-FileCopyrightText: 2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_WEBSERVER_CONTROL_H
#define DOSBOX_WEBSERVER_CONTROL_H

#include "bridge.h"
#include "libs/http/http.h"

namespace Webserver {

class ShutdownCommand : public Command {
public:
	void Execute() override;
	static void Post(const httplib::Request& req, httplib::Response& res);
};

struct ControlHandlers {
	static void GetProgramState(const httplib::Request& req, httplib::Response& res);
	static void GetStatus(const httplib::Request& req, httplib::Response& res);
};

} // namespace Webserver

#endif // DOSBOX_WEBSERVER_CONTROL_H
