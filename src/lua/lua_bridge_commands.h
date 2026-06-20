// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_LUA_BRIDGE_COMMANDS_H
#define DOSBOX_LUA_BRIDGE_COMMANDS_H

#include "lua/lua_coroutine.h"
#include "lua/lua_debug_log.h"
#include "lua/lua_engine.h"
#include "lua/script_validator.h"
#include "webserver/bridge.h"

#include "libs/http/http.h"

#include <chrono>
#include <mutex>
#include <string>

namespace Lua {

class ScriptManager {
public:
	static ScriptManager& Instance();

	LuaEngine& Engine()
	{
		return engine;
	}
	LuaCoroutine& Coroutine()
	{
		return coroutine;
	}
	DebugLog& Log()
	{
		return debug_log;
	}
	ScriptParams& Params()
	{
		return params;
	}

	void DispatchFrame(uint64_t frame_number);

private:
	ScriptManager() = default;

	LuaEngine engine;
	LuaCoroutine coroutine{engine};
	DebugLog debug_log;
	ScriptParams params;

	ScriptManager(const ScriptManager&)            = delete;
	ScriptManager& operator=(const ScriptManager&) = delete;
};

class ScriptRateLimiter {
public:
	bool ShouldReject(int64_t& retry_after_ms);

private:
	using Clock                            = std::chrono::steady_clock;
	static constexpr int64_t MinIntervalMs = 2000;

	Clock::time_point last_load = {};
	std::mutex mtx;
};

class LuaLoadCommand : public Webserver::Command {
public:
	LuaLoadCommand(std::string source, ScriptParams params);
	void Execute() override;
	static void Post(const httplib::Request& req, httplib::Response& res);

private:
	std::string source;
	ScriptParams params;
};

class LuaStartCommand : public Webserver::Command {
public:
	void Execute() override;
	static void Post(const httplib::Request& req, httplib::Response& res);
};

class LuaStopCommand : public Webserver::Command {
public:
	void Execute() override;
	static void Post(const httplib::Request& req, httplib::Response& res);
};

struct LuaStatusResult {
	ScriptState state = ScriptState::Idle;
	std::string error = {};
	uint64_t frame    = 0;
	std::string name  = {};
};

class LuaStatusCommand : public Webserver::Command {
public:
	void Execute() override;
	static void Get(const httplib::Request& req, httplib::Response& res);

	LuaStatusResult result = {};
};

} // namespace Lua

void LuaDispatchFrame(uint64_t frame_number);

#endif
