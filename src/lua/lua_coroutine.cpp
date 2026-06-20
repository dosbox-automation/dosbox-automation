// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "lua/lua_coroutine.h"
#include "lua/lua_api.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

namespace Lua {

static constexpr const char* CoroutineKey = "LuaCoroutine";

const char* ScriptStateName(const ScriptState s)
{
	switch (s) {
	case ScriptState::Idle: return "idle";
	case ScriptState::Loaded: return "loaded";
	case ScriptState::Running: return "running";
	case ScriptState::Yielded: return "yielded";
	case ScriptState::Completed: return "completed";
	case ScriptState::Error: return "error";
	}
	return "unknown";
}

LuaCoroutine::LuaCoroutine(LuaEngine& engine) : engine(engine) {}

LuaCoroutine::~LuaCoroutine()
{
	Cleanup();
}

void LuaCoroutine::Cleanup()
{
	auto* L = engine.GetState();
	if (L && coroutine_ref != 0) {
		luaL_unref(L, LUA_REGISTRYINDEX, coroutine_ref);
		coroutine_ref = 0;
	}
	coroutine        = nullptr;
	wait_until_frame = 0;
	waiting_for_text = false;
	wait_text_pattern.clear();
	error_msg.clear();
}

bool LuaCoroutine::CheckWaitForText()
{
	const auto text = ReadScreenText();
	if (MatchSubstring(text, wait_text_pattern, wait_text_ignorecase)) {
		waiting_for_text = false;
		// Push true onto coroutine stack so the resumed wait_for_text
		// call returns true to the script.
		lua_pushboolean(coroutine, true);
		return true;
	}

	if (current_frame >= wait_text_deadline) {
		waiting_for_text = false;
		lua_pushboolean(coroutine, false);
		return true;
	}

	return false;
}

void LuaCoroutine::RegisterApi()
{
	auto* L = engine.GetState();
	if (!L) {
		return;
	}

	lua_getglobal(L, "dosbox");
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
		lua_setglobal(L, "dosbox");
		lua_getglobal(L, "dosbox");
	}

	lua_pushlightuserdata(L, this);
	lua_setfield(L, LUA_REGISTRYINDEX, CoroutineKey);

	lua_pushcfunction(L, LuaWaitFrames);
	lua_setfield(L, -2, "wait_frames");

	lua_pushcfunction(L, LuaFrame);
	lua_setfield(L, -2, "frame");

	lua_pop(L, 1);

	RegisterDosboxApi(L, this, debug_log);
}

bool LuaCoroutine::Start(DebugLog* log)
{
	auto* L = engine.GetState();
	if (!L || !engine.HasLoadedScript()) {
		return false;
	}

	debug_log = log;
	Cleanup();
	RegisterApi();

	lua_getfield(L, LUA_REGISTRYINDEX, "LoadedChunk");
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		return false;
	}

	coroutine     = lua_newthread(L);
	coroutine_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	// Move the chunk from the main state to the coroutine stack.
	lua_xmove(L, coroutine, 1);

	state         = ScriptState::Running;
	current_frame = 0;
	return true;
}

ScriptState LuaCoroutine::DispatchFrame(const uint64_t frame_number)
{
	current_frame = frame_number;

	if (state == ScriptState::Yielded) {
		if (waiting_for_text) {
			if (!CheckWaitForText()) {
				return state;
			}
			// CheckWaitForText pushed a boolean onto the coroutine
			// stack. Resume with 1 result so wait_for_text returns it.
			state = ScriptState::Running;

			int nresults = 0;
			const auto status = lua_resume(coroutine, nullptr, 1, &nresults);

			if (status == LUA_YIELD) {
				state = ScriptState::Yielded;
				lua_pop(coroutine, nresults);
			} else if (status == LUA_OK) {
				state = ScriptState::Completed;
				lua_pop(coroutine, nresults);
			} else {
				state     = ScriptState::Error;
				error_msg = lua_tostring(coroutine, -1);
				lua_pop(coroutine, 1);
			}
			return state;
		}

		if (current_frame < wait_until_frame) {
			return state;
		}
		state = ScriptState::Running;
	}

	if (state != ScriptState::Running) {
		return state;
	}

	if (!coroutine) {
		state     = ScriptState::Error;
		error_msg = "coroutine is null";
		return state;
	}

	int nresults      = 0;
	const auto status = lua_resume(coroutine, nullptr, 0, &nresults);

	if (status == LUA_YIELD) {
		state = ScriptState::Yielded;
		lua_pop(coroutine, nresults);
	} else if (status == LUA_OK) {
		state = ScriptState::Completed;
		lua_pop(coroutine, nresults);
	} else {
		state     = ScriptState::Error;
		error_msg = lua_tostring(coroutine, -1);
		lua_pop(coroutine, 1);
	}

	return state;
}

void LuaCoroutine::Stop()
{
	if (state == ScriptState::Running || state == ScriptState::Yielded) {
		Cleanup();
		state = ScriptState::Idle;
	}
}

int LuaCoroutine::LuaWaitFrames(lua_State* L)
{
	const auto n = static_cast<int64_t>(luaL_checkinteger(L, 1));
	if (n < 0) {
		return luaL_error(L, "wait_frames: count must be non-negative");
	}

	lua_getfield(L, LUA_REGISTRYINDEX, CoroutineKey);
	auto* self = static_cast<LuaCoroutine*>(lua_touserdata(L, -1));
	lua_pop(L, 1);

	if (!self) {
		return luaL_error(L, "wait_frames: no coroutine context");
	}

	self->wait_until_frame = self->current_frame + static_cast<uint64_t>(n);
	return lua_yield(L, 0);
}

int LuaCoroutine::LuaFrame(lua_State* L)
{
	lua_getfield(L, LUA_REGISTRYINDEX, CoroutineKey);
	auto* self = static_cast<LuaCoroutine*>(lua_touserdata(L, -1));
	lua_pop(L, 1);

	if (!self) {
		return luaL_error(L, "frame: no coroutine context");
	}

	lua_pushinteger(L, static_cast<lua_Integer>(self->current_frame));
	return 1;
}

} // namespace Lua
