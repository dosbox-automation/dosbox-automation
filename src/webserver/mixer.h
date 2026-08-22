// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_WEBSERVER_MIXER_H
#define DOSBOX_WEBSERVER_MIXER_H

#include "webserver/bridge.h"
#include "http/http.h"

#include "audio/audio_frame.h"

#include <map>
#include <string>

namespace Webserver {

struct MixerChannelInfo {
	std::string name       = {};
	AudioFrame user_volume = {};
	AudioFrame app_volume  = {};
	bool enabled           = false;
	int sample_rate_hz     = 0;
};

class MixerStatusCommand : public Command {
public:
	void Execute() override;
	static void Get(const httplib::Request& req, httplib::Response& res);

	AudioFrame master_volume = {};
	bool muted               = false;
	int sample_rate_hz       = 0;
	std::vector<MixerChannelInfo> channels = {};
};

class MixerSetVolumeCommand : public Command {
public:
	void Execute() override;
	static void Put(const httplib::Request& req, httplib::Response& res);

	AudioFrame volume = {};
};

class MixerMuteCommand : public Command {
public:
	void Execute() override;
	static void Post(const httplib::Request& req, httplib::Response& res);
};

class MixerUnmuteCommand : public Command {
public:
	void Execute() override;
	static void Post(const httplib::Request& req, httplib::Response& res);
};

class MixerSetChannelVolumeCommand : public Command {
public:
	void Execute() override;
	static void Put(const httplib::Request& req, httplib::Response& res);

	std::string channel_name = {};
	AudioFrame volume        = {};
	bool channel_found       = false;
};

} // namespace Webserver

#endif // DOSBOX_WEBSERVER_MIXER_H
