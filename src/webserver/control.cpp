// SPDX-FileCopyrightText: 2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "control.h"
#include "bridge.h"
#include "webserver.h"

#include "dosbox.h"
#include "gui/titlebar.h"
#include "shell/shell.h"

#include "libs/json/json.h"

using json = nlohmann::json;

namespace Webserver {

void ShutdownCommand::Execute()
{
	DOSBOX_RequestShutdown();
}

void ShutdownCommand::Post(const httplib::Request&, httplib::Response& res)
{
	ShutdownCommand cmd;
	cmd.WaitForCompletion();

	json j;
	j["status"] = "shutdown_requested";
	send_json(res, j);
}

void ControlHandlers::GetProgramState(const httplib::Request&, httplib::Response& res)
{
	json j;
	j["segment_name"] = TITLEBAR_GetSegmentName();
	j["canonical_name"] = TITLEBAR_GetCanonicalName();
	j["is_shell"] = SHELL_IsRunning();
	j["is_booted"] = TITLEBAR_IsBooted();
	send_json(res, j);
}

void ControlHandlers::GetStatus(const httplib::Request&, httplib::Response& res)
{
	json j;
	j["running"] = true;
	j["shutdown_requested"] = DOSBOX_IsShutdownRequested();
	j["is_booted"] = TITLEBAR_IsBooted();
	j["program"] = TITLEBAR_GetSegmentName();
	j["canonical_name"] = TITLEBAR_GetCanonicalName();
	j["is_shell"] = SHELL_IsRunning();
	send_json(res, j);
}

} // namespace Webserver
