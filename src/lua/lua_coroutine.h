// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_LUA_COROUTINE_H
#define DOSBOX_LUA_COROUTINE_H

#include "lua/lua_engine.h"

#include <cstdint>
#include <string>

struct lua_State;

namespace Lua {

enum class ScriptState { Idle, Loaded, Running, Yielded, Completed, Error };

const char* ScriptStateName(ScriptState s);

class LuaCoroutine {
public:
	explicit LuaCoroutine(LuaEngine& engine);
	~LuaCoroutine();

	LuaCoroutine(const LuaCoroutine&)            = delete;
	LuaCoroutine& operator=(const LuaCoroutine&) = delete;

	bool Start();
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

private:
	LuaEngine& engine;
	lua_State* coroutine      = nullptr;
	int coroutine_ref         = 0;
	ScriptState state         = ScriptState::Idle;
	uint64_t current_frame    = 0;
	uint64_t wait_until_frame = 0;
	std::string error_msg     = {};

	void RegisterApi();
	void Cleanup();

	static int LuaWaitFrames(lua_State* L);
	static int LuaFrame(lua_State* L);
};

} // namespace Lua

#endif
