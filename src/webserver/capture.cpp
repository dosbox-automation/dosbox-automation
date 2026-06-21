// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "capture.h"
#include "bridge.h"
#include "webserver.h"

#include "capture/capture.h"

#include "libs/json/json.h"

using json = nlohmann::json;

namespace Webserver {

// --- CaptureStartCommand ---

void CaptureStartCommand::Execute()
{
	CAPTURE_StartVideoCapture();
}

void CaptureStartCommand::Post(const httplib::Request&, httplib::Response& res)
{
	CaptureStartCommand cmd;
	cmd.WaitForCompletion(1000);

	if (!cmd.error.empty()) {
		res.status = 500;
		json err;
		err["error"] = cmd.error;
		send_json(res, err);
		return;
	}

	json result;
	result["status"] = "recording";
	send_json(res, result);
}

// --- CaptureStopCommand ---

void CaptureStopCommand::Execute()
{
	CAPTURE_StopVideoCapture();
}

void CaptureStopCommand::Post(const httplib::Request&, httplib::Response& res)
{
	CaptureStopCommand cmd;
	cmd.WaitForCompletion(1000);

	if (!cmd.error.empty()) {
		res.status = 500;
		json err;
		err["error"] = cmd.error;
		send_json(res, err);
		return;
	}

	json result;
	result["status"] = "stopped";
	send_json(res, result);
}

// --- CaptureStatusCommand ---

void CaptureStatusCommand::Execute()
{
	capturing = CAPTURE_IsCapturingVideo();
}

void CaptureStatusCommand::Get(const httplib::Request&, httplib::Response& res)
{
	CaptureStatusCommand cmd;
	cmd.WaitForCompletion(250);

	if (!cmd.error.empty()) {
		res.status = 500;
		json err;
		err["error"] = cmd.error;
		send_json(res, err);
		return;
	}

	json result;
	result["capturing"] = cmd.capturing;
	send_json(res, result);
}

} // namespace Webserver
