// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "lua/lua_bridge_commands.h"

#include "gui/osd/osd.h"
#include "webserver/webserver.h"

#include "libs/json/json.h"

#include "misc/cross.h"
#include "misc/logging.h"

using json = nlohmann::json;

namespace Lua {

// -- ScriptManager --

ScriptManager& ScriptManager::Instance()
{
	static ScriptManager instance;
	return instance;
}

void ScriptManager::DispatchFrame(const uint64_t frame_number)
{
	const auto prev_state = coroutine.State();
	coroutine.DispatchFrame(frame_number);
	const auto new_state = coroutine.State();

	const bool running = (new_state == ScriptState::Running ||
	                      new_state == ScriptState::Yielded);
	OSD::OsdManager::Instance().SetIcon(OSD::IconId::ScriptRunning, running);

	if (prev_state != new_state && (new_state == ScriptState::Completed ||
	                                new_state == ScriptState::Error)) {
		if (debug_log.IsOpen()) {
			debug_log.Trace(frame_number,
			                "script %s: %s",
			                ScriptStateName(new_state),
			                new_state == ScriptState::Error
			                        ? coroutine.ErrorMessage().c_str()
			                        : "ok");
			debug_log.Close();
		}
		LOG_MSG("LUA: Script '%s' %s",
		        params.name.c_str(),
		        ScriptStateName(new_state));
	}
}

// -- ScriptRateLimiter --

bool ScriptRateLimiter::ShouldReject(int64_t& retry_after_ms)
{
	std::lock_guard<std::mutex> lock(mtx);
	const auto now = Clock::now();
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
	        now - last_load);

	if (last_load != Clock::time_point{} && elapsed.count() < MinIntervalMs) {
		retry_after_ms = MinIntervalMs - elapsed.count();
		return true;
	}

	last_load = now;
	return false;
}

// -- LuaLoadCommand --

LuaLoadCommand::LuaLoadCommand(std::string source, ScriptParams params)
        : source(std::move(source)),
          params(std::move(params))
{}

void LuaLoadCommand::Execute()
{
	auto& mgr = ScriptManager::Instance();

	const auto state = mgr.Coroutine().State();
	if (state == ScriptState::Running || state == ScriptState::Yielded) {
		error = "a script is already running";
		return;
	}

	mgr.Engine().Reset();
	mgr.Params() = params;

	auto result = mgr.Engine().LoadScript(source, params.name);
	if (!result.ok) {
		error = result.error;
		return;
	}

	if (params.debug) {
		const auto log_dir = (get_config_dir() / "logs").string();
		mgr.Log().Open(log_dir, params.name);
		if (mgr.Log().IsOpen()) {
			LOG_MSG("LUA: Debug log at %s",
			        mgr.Log().FilePath().c_str());
		}
	} else {
		mgr.Log().Close();
	}

	LOG_MSG("LUA: Script '%s' loaded", params.name.c_str());
}

void LuaLoadCommand::Post(const httplib::Request& req, httplib::Response& res)
{
	static ScriptRateLimiter rate_limiter;
	int64_t retry_after_ms = 0;
	if (rate_limiter.ShouldReject(retry_after_ms)) {
		res.status = 429;
		res.set_header("Retry-After",
		               std::to_string((retry_after_ms + 999) / 1000));
		json err;
		err["error"] = "too many requests";
		Webserver::send_json(res, err);
		return;
	}

	const auto ct_check = ScriptValidator::ValidateContentType(
	        req.get_header_value("Content-Type"));
	if (!ct_check.ok) {
		res.status = ct_check.http_status;
		json err;
		err["error"] = ct_check.error;
		Webserver::send_json(res, err);
		return;
	}

	const auto body_check = ScriptValidator::ValidateBody(req.body);
	if (!body_check.ok) {
		res.status = body_check.http_status;
		json err;
		err["error"] = body_check.error;
		Webserver::send_json(res, err);
		return;
	}

	ScriptParams params;
	const auto param_check =
	        ScriptValidator::ValidateParams(req.get_param_value("name"),
	                                        req.get_param_value("seed"),
	                                        req.get_param_value("debug"),
	                                        params);
	if (!param_check.ok) {
		res.status = param_check.http_status;
		json err;
		err["error"] = param_check.error;
		Webserver::send_json(res, err);
		return;
	}

	LuaLoadCommand cmd(req.body, params);
	cmd.WaitForCompletion(5000);

	if (!cmd.error.empty()) {
		res.status = 400;
		json err;
		err["error"] = cmd.error;
		Webserver::send_json(res, err);
		return;
	}

	json result;
	result["status"] = "loaded";
	result["name"]   = params.name;
	Webserver::send_json(res, result);
}

// -- LuaStartCommand --

void LuaStartCommand::Execute()
{
	auto& mgr = ScriptManager::Instance();

	if (!mgr.Engine().HasLoadedScript()) {
		error = "no script loaded";
		return;
	}

	const auto state = mgr.Coroutine().State();
	if (state == ScriptState::Running || state == ScriptState::Yielded) {
		error = "script is already running";
		return;
	}

	if (!mgr.Coroutine().Start()) {
		error = "failed to create coroutine";
		return;
	}

	if (mgr.Log().IsOpen()) {
		mgr.Log().Trace(0, "script started: %s", mgr.Params().name.c_str());
	}

	LOG_MSG("LUA: Script '%s' started", mgr.Params().name.c_str());
}

void LuaStartCommand::Post(const httplib::Request&, httplib::Response& res)
{
	LuaStartCommand cmd;
	cmd.WaitForCompletion(5000);

	if (!cmd.error.empty()) {
		res.status = 400;
		json err;
		err["error"] = cmd.error;
		Webserver::send_json(res, err);
		return;
	}

	json result;
	result["status"] = "started";
	Webserver::send_json(res, result);
}

// -- LuaStopCommand --

void LuaStopCommand::Execute()
{
	auto& mgr = ScriptManager::Instance();
	mgr.Coroutine().Stop();
	mgr.Log().Close();

	LOG_MSG("LUA: Script stopped");
}

void LuaStopCommand::Post(const httplib::Request&, httplib::Response& res)
{
	LuaStopCommand cmd;
	cmd.WaitForCompletion(5000);

	if (!cmd.error.empty()) {
		res.status = 400;
		json err;
		err["error"] = cmd.error;
		Webserver::send_json(res, err);
		return;
	}

	json result;
	result["status"] = "stopped";
	Webserver::send_json(res, result);
}

// -- LuaStatusCommand --

void LuaStatusCommand::Execute()
{
	auto& mgr    = ScriptManager::Instance();
	result.state = mgr.Coroutine().State();
	result.error = mgr.Coroutine().ErrorMessage();
	result.frame = mgr.Coroutine().CurrentFrame();
	result.name  = mgr.Params().name;
}

void LuaStatusCommand::Get(const httplib::Request&, httplib::Response& res)
{
	LuaStatusCommand cmd;
	cmd.WaitForCompletion(5000);

	json j;
	j["state"] = ScriptStateName(cmd.result.state);
	j["frame"] = cmd.result.frame;
	j["name"]  = cmd.result.name;

	if (!cmd.result.error.empty()) {
		j["error"] = cmd.result.error;
	}

	Webserver::send_json(res, j);
}

} // namespace Lua

void LuaDispatchFrame(const uint64_t frame_number)
{
	Lua::ScriptManager::Instance().DispatchFrame(frame_number);
}
