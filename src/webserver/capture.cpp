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
	CAPTURE_StartVideoCapture(mode);
}

void CaptureStartCommand::Post(const httplib::Request& req, httplib::Response& res)
{
	CaptureStartCommand cmd;

	// The body is optional; without one the raw framebuffer is
	// captured, matching the pre-mode behaviour
	std::string mode_str = "raw";
	if (!req.body.empty()) {
		const auto j = json::parse(req.body);
		mode_str     = j.value("mode", "raw");
	}

	if (mode_str == "rendered") {
		cmd.mode = VideoCaptureMode::Rendered;
	} else if (mode_str != "raw") {
		res.status = 400;
		json err;
		err["error"] = "mode must be 'raw' or 'rendered'";
		send_json(res, err);
		return;
	}

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
	result["mode"]   = mode_str;
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
	capturing  = CAPTURE_IsCapturingVideo();
	mode       = CAPTURE_GetVideoCaptureMode();
	end_reason = CAPTURE_GetVideoCaptureEndReason();
}

static const char* to_string(const VideoCaptureEndReason reason)
{
	switch (reason) {
	case VideoCaptureEndReason::NotEnded: return "none";
	case VideoCaptureEndReason::CleanStop: return "clean";
	case VideoCaptureEndReason::WriteError: return "write_error";
	case VideoCaptureEndReason::DiskSpaceLow: return "disk_space_low";
	}
	return "none";
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
	result["mode"] = (cmd.mode == VideoCaptureMode::Rendered) ? "rendered"
	                                                          : "raw";
	result["last_stop_reason"] = to_string(cmd.end_reason);
	send_json(res, result);
}

} // namespace Webserver
