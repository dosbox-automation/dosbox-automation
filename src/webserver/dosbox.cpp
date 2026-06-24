// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "private/dosbox.h"

#include "dosbox.h"

namespace Webserver {

void ShutdownCommand::Execute()
{
	DOSBOX_RequestShutdown();
}

void ShutdownCommand::Post(const httplib::Request&, httplib::Response&)
{
	ShutdownCommand cmd;
	cmd.WaitForCompletion();
}

} // namespace Webserver
