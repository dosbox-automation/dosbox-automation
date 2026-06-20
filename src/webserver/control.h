// SPDX-FileCopyrightText: 2026 The DOSBox Staging Team
// SPDX-FileCopyrightText: 2026 dosbox-automation Project
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_WEBSERVER_CONTROL_H
#define DOSBOX_WEBSERVER_CONTROL_H

#include "private/dosbox.h"

#include "http/http.h"

namespace Webserver {

struct ControlHandlers {
	static void GetProgramState(const httplib::Request& req, httplib::Response& res);
	static void GetStatus(const httplib::Request& req, httplib::Response& res);
};

} // namespace Webserver

#endif // DOSBOX_WEBSERVER_CONTROL_H
