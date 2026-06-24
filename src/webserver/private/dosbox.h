// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_WEBSERVER_DOSBOX_H
#define DOSBOX_WEBSERVER_DOSBOX_H

#include "webserver/bridge.h"

#include "http/http.h"

namespace Webserver {

class ShutdownCommand : public Command {
public:
	void Execute() override;
	static void Post(const httplib::Request& req, httplib::Response& res);
};

} // namespace Webserver

#endif // DOSBOX_WEBSERVER_DOSBOX_H
