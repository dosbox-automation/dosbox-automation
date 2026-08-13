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

// --- CaptureCompressionGetCommand ---

void CaptureCompressionGetCommand::Execute()
{
	raw_level = CAPTURE_GetVideoCompressionLevel(VideoCaptureMode::Raw);
	rendered_level = CAPTURE_GetVideoCompressionLevel(VideoCaptureMode::Rendered);
}

void CaptureCompressionGetCommand::Get(const httplib::Request&, httplib::Response& res)
{
	CaptureCompressionGetCommand cmd;
	cmd.WaitForCompletion(250);

	if (!cmd.error.empty()) {
		res.status = 500;
		json err;
		err["error"] = cmd.error;
		send_json(res, err);
		return;
	}

	json result;
	result["raw"]      = cmd.raw_level;
	result["rendered"] = cmd.rendered_level;
	send_json(res, result);
}

// --- CaptureCompressionSetCommand ---

void CaptureCompressionSetCommand::Execute()
{
	// The level is latched at capture start (deflateInit2); changing it
	// mid-recording would silently not apply, so refuse instead
	if (CAPTURE_IsCapturingVideo()) {
		rejected_recording_active = true;
		return;
	}
	if (raw_level >= 0) {
		CAPTURE_SetVideoCompressionLevel(VideoCaptureMode::Raw, raw_level);
	}
	if (rendered_level >= 0) {
		CAPTURE_SetVideoCompressionLevel(VideoCaptureMode::Rendered,
		                                 rendered_level);
	}
	raw_level = CAPTURE_GetVideoCompressionLevel(VideoCaptureMode::Raw);
	rendered_level = CAPTURE_GetVideoCompressionLevel(VideoCaptureMode::Rendered);
}

void CaptureCompressionSetCommand::Put(const httplib::Request& req, httplib::Response& res)
{
	CaptureCompressionSetCommand cmd;

	const auto j = json::parse(req.body);

	if (!j.contains("raw") && !j.contains("rendered")) {
		res.status = 400;
		json err;
		err["error"] = "body must contain 'raw' and/or 'rendered'";
		send_json(res, err);
		return;
	}

	for (const auto* key : {"raw", "rendered"}) {
		if (!j.contains(key)) {
			continue;
		}
		if (!j[key].is_number_integer() || j[key] < 0 || j[key] > 9) {
			res.status = 400;
			json err;
			err["error"] = std::string(key) +
			               " must be an integer from 0 to 9";
			send_json(res, err);
			return;
		}
	}

	if (j.contains("raw")) {
		cmd.raw_level = j["raw"];
	}
	if (j.contains("rendered")) {
		cmd.rendered_level = j["rendered"];
	}

	cmd.WaitForCompletion(250);

	if (!cmd.error.empty()) {
		res.status = 500;
		json err;
		err["error"] = cmd.error;
		send_json(res, err);
		return;
	}

	if (cmd.rejected_recording_active) {
		res.status = 409;
		json err;
		err["error"] = "cannot change compression levels while a video capture is running";
		send_json(res, err);
		return;
	}

	json result;
	result["raw"]      = cmd.raw_level;
	result["rendered"] = cmd.rendered_level;
	send_json(res, result);
}

} // namespace Webserver
