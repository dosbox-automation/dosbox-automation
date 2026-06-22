// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_LUA_COROUTINE_H
#define DOSBOX_LUA_COROUTINE_H

#include "lua/lua_engine.h"

#include <chrono>
#include <cstdint>
#include <string>

struct lua_State;

namespace Lua {

class DebugLog;

enum class ScriptState { Idle, Loaded, Running, Yielded, Completed, Error };

const char* ScriptStateName(ScriptState s);

class LuaCoroutine {
public:
	explicit LuaCoroutine(LuaEngine& engine);
	~LuaCoroutine();

	LuaCoroutine(const LuaCoroutine&)            = delete;
	LuaCoroutine& operator=(const LuaCoroutine&) = delete;

	bool Start(DebugLog* debug_log = nullptr);
	ScriptState DispatchFrame(uint64_t frame_number);
	void Stop();

	ScriptState State() const
	{
		return state;
	}
	uint64_t CurrentFrame() const
	{
		return current_frame;
	}
	const std::string& ErrorMessage() const
	{
		return error_msg;
	}

	void SetWaitForText(const std::string& pattern, bool ignorecase,
	                    uint64_t deadline);

private:
	LuaEngine& engine;
	DebugLog* debug_log       = nullptr;
	lua_State* coroutine      = nullptr;
	int coroutine_ref         = 0;
	ScriptState state         = ScriptState::Idle;
	uint64_t current_frame    = 0;
	uint64_t wait_until_frame = 0;
	std::string error_msg     = {};

	// wait_for_text polling state
	bool waiting_for_text                                         = false;
	std::string wait_text_pattern                                 = {};
	std::string wait_text_pattern_lower                           = {};
	bool wait_text_ignorecase                                     = false;
	uint64_t wait_text_deadline                                   = 0;
	std::chrono::steady_clock::time_point wait_text_wall_deadline = {};

	void RegisterApi();
	void Cleanup();
	bool CheckWaitForText();

	static int LuaWaitFrames(lua_State* L);
	static int LuaFrame(lua_State* L);
};

} // namespace Lua

#endif
