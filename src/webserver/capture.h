// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_WEBSERVER_CAPTURE_H
#define DOSBOX_WEBSERVER_CAPTURE_H

#include "webserver/bridge.h"
#include "libs/http/http.h"

#include "capture/capture.h"

namespace Webserver {

class CaptureStartCommand : public Command {
public:
	void Execute() override;
	static void Post(const httplib::Request& req, httplib::Response& res);

	VideoCaptureMode mode = VideoCaptureMode::Raw;
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
	VideoCaptureMode mode = VideoCaptureMode::Raw;
	VideoCaptureEndReason end_reason = VideoCaptureEndReason::NotEnded;
};

class CaptureCompressionGetCommand : public Command {
public:
	void Execute() override;
	static void Get(const httplib::Request& req, httplib::Response& res);

	int raw_level      = 0;
	int rendered_level = 0;
};

class CaptureCompressionSetCommand : public Command {
public:
	void Execute() override;
	static void Put(const httplib::Request& req, httplib::Response& res);

	// -1 = leave unchanged; filled with the applied values on success
	int raw_level      = -1;
	int rendered_level = -1;
	bool rejected_recording_active = false;
};

} // namespace Webserver

#endif // DOSBOX_WEBSERVER_CAPTURE_H
