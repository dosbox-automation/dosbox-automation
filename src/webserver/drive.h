// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_WEBSERVER_DRIVE_H
#define DOSBOX_WEBSERVER_DRIVE_H

#include "bridge.h"
#include "libs/http/http.h"

#include <string>

namespace Webserver {

class DriveSwapCommand : public Command {
public:
	DriveSwapCommand(char drive_letter, std::string image_path);
	void Execute() override;
	static void Post(const httplib::Request& req, httplib::Response& res);

private:
	char drive_letter = 'A';
	std::string image_path = {};
};

} // namespace Webserver

#endif // DOSBOX_WEBSERVER_DRIVE_H
