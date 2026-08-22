// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "mixer.h"
#include "bridge.h"
#include "webserver.h"

#include "audio/mixer.h"

#include "json/json.h"

using json = nlohmann::json;

namespace Webserver {

// --- MixerStatusCommand ---

void MixerStatusCommand::Execute()
{
	master_volume  = MIXER_GetMasterVolume();
	muted          = MIXER_IsManuallyMuted();
	sample_rate_hz = MIXER_GetSampleRate();

	for (const auto& [name, ch] : MIXER_GetChannels()) {
		MixerChannelInfo info;
		info.name           = ch->GetName();
		info.user_volume    = ch->GetUserVolume();
		info.app_volume     = ch->GetAppVolume();
		info.enabled        = ch->is_enabled.load();
		info.sample_rate_hz = ch->GetSampleRate();
		channels.push_back(std::move(info));
	}
}

void MixerStatusCommand::Get(const httplib::Request&, httplib::Response& res)
{
	MixerStatusCommand cmd;
	cmd.WaitForCompletion(500);

	if (!cmd.error.empty()) {
		res.status = 500;
		json err;
		err["error"] = cmd.error;
		send_json(res, err);
		return;
	}

	json result;
	result["master_volume"] = {
		{"left", cmd.master_volume.left},
		{"right", cmd.master_volume.right}
	};
	result["muted"]       = cmd.muted;
	result["sample_rate"] = cmd.sample_rate_hz;

	auto ch_array = json::array();
	for (const auto& ch : cmd.channels) {
		json entry;
		entry["name"]        = ch.name;
		entry["user_volume"] = {
			{"left", ch.user_volume.left},
			{"right", ch.user_volume.right}
		};
		entry["app_volume"] = {
			{"left", ch.app_volume.left},
			{"right", ch.app_volume.right}
		};
		entry["enabled"]     = ch.enabled;
		entry["sample_rate"] = ch.sample_rate_hz;
		ch_array.push_back(std::move(entry));
	}
	result["channels"] = std::move(ch_array);

	send_json(res, result);
}

// --- MixerSetVolumeCommand ---

void MixerSetVolumeCommand::Execute()
{
	MIXER_SetMasterVolume(volume);
}

void MixerSetVolumeCommand::Put(const httplib::Request& req, httplib::Response& res)
{
	MixerSetVolumeCommand cmd;

	const auto j = json::parse(req.body);

	if (!j.contains("left") || !j.contains("right")) {
		res.status = 400;
		json err;
		err["error"] = "body must contain 'left' and 'right' volume values";
		send_json(res, err);
		return;
	}

	if (!j["left"].is_number() || !j["right"].is_number()) {
		res.status = 400;
		json err;
		err["error"] = "'left' and 'right' must be numbers";
		send_json(res, err);
		return;
	}

	cmd.volume.left  = j["left"].get<float>();
	cmd.volume.right = j["right"].get<float>();

	if (cmd.volume.left < 0.0f || cmd.volume.right < 0.0f) {
		res.status = 400;
		json err;
		err["error"] = "volume values must be non-negative";
		send_json(res, err);
		return;
	}

	cmd.WaitForCompletion(500);

	if (!cmd.error.empty()) {
		res.status = 500;
		json err;
		err["error"] = cmd.error;
		send_json(res, err);
		return;
	}

	json result;
	result["left"]  = cmd.volume.left;
	result["right"] = cmd.volume.right;
	send_json(res, result);
}

// --- MixerMuteCommand ---

void MixerMuteCommand::Execute()
{
	MIXER_Mute();
}

void MixerMuteCommand::Post(const httplib::Request&, httplib::Response& res)
{
	MixerMuteCommand cmd;
	cmd.WaitForCompletion(500);

	if (!cmd.error.empty()) {
		res.status = 500;
		json err;
		err["error"] = cmd.error;
		send_json(res, err);
		return;
	}

	json result;
	result["muted"] = true;
	send_json(res, result);
}

// --- MixerUnmuteCommand ---

void MixerUnmuteCommand::Execute()
{
	MIXER_Unmute();
}

void MixerUnmuteCommand::Post(const httplib::Request&, httplib::Response& res)
{
	MixerUnmuteCommand cmd;
	cmd.WaitForCompletion(500);

	if (!cmd.error.empty()) {
		res.status = 500;
		json err;
		err["error"] = cmd.error;
		send_json(res, err);
		return;
	}

	json result;
	result["muted"] = false;
	send_json(res, result);
}

// --- MixerSetChannelVolumeCommand ---

void MixerSetChannelVolumeCommand::Execute()
{
	auto ch = MIXER_FindChannel(channel_name.c_str());
	if (!ch) {
		channel_found = false;
		return;
	}
	channel_found = true;
	ch->SetUserVolume(volume);
}

void MixerSetChannelVolumeCommand::Put(const httplib::Request& req, httplib::Response& res)
{
	MixerSetChannelVolumeCommand cmd;

	cmd.channel_name = req.path_params.at("name");
	if (cmd.channel_name.empty()) {
		res.status = 400;
		json err;
		err["error"] = "channel name must not be empty";
		send_json(res, err);
		return;
	}

	const auto j = json::parse(req.body);

	if (!j.contains("left") || !j.contains("right")) {
		res.status = 400;
		json err;
		err["error"] = "body must contain 'left' and 'right' volume values";
		send_json(res, err);
		return;
	}

	if (!j["left"].is_number() || !j["right"].is_number()) {
		res.status = 400;
		json err;
		err["error"] = "'left' and 'right' must be numbers";
		send_json(res, err);
		return;
	}

	cmd.volume.left  = j["left"].get<float>();
	cmd.volume.right = j["right"].get<float>();

	if (cmd.volume.left < 0.0f || cmd.volume.right < 0.0f) {
		res.status = 400;
		json err;
		err["error"] = "volume values must be non-negative";
		send_json(res, err);
		return;
	}

	cmd.WaitForCompletion(500);

	if (!cmd.error.empty()) {
		res.status = 500;
		json err;
		err["error"] = cmd.error;
		send_json(res, err);
		return;
	}

	if (!cmd.channel_found) {
		res.status = 404;
		json err;
		err["error"] = "mixer channel not found: " + cmd.channel_name;
		send_json(res, err);
		return;
	}

	json result;
	result["channel"] = cmd.channel_name;
	result["left"]    = cmd.volume.left;
	result["right"]   = cmd.volume.right;
	send_json(res, result);
}

} // namespace Webserver
