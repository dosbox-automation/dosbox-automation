// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_LUA_COROUTINE_H
#define DOSBOX_LUA_COROUTINE_H

#include "lua/lua_engine.h"

#include "hardware/input/keyboard.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

struct lua_State;

namespace Lua {

class DebugLog;

enum class ScriptState { Idle, Loaded, Running, Yielded, Completed, Error };

// One logical key stroke for paced text injection: a key plus whether Shift
// must be held for it. Expanded to press/release events when drained.
struct KeyStroke {
	KBD_KEYS key = KBD_NONE;
	bool shift   = false;
};

const char* ScriptStateName(ScriptState s);

class LuaCoroutine {
public:
	explicit LuaCoroutine(LuaEngine& engine);
	~LuaCoroutine();

	LuaCoroutine(const LuaCoroutine&)            = delete;
	LuaCoroutine& operator=(const LuaCoroutine&) = delete;

	bool Start(DebugLog* debug_log = nullptr);
	ScriptState DispatchFrame(uint64_t frame_number);

	// Off-frame wall-clock timeout for a yielded wait/type, for when frames
	// have stalled and DispatchFrame no longer runs. Call between frames.
	ScriptState ReapStalledWaits();

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

	// Paced keystroke queue; drains across frames gated on buffer room.
	void QueueTypeInput(std::vector<KeyStroke> strokes);

	// Override the wall-clock ceiling (default 30s); tests use a short value.
	void SetWallCeiling(std::chrono::steady_clock::duration ceiling)
	{
		wall_ceiling = ceiling;
	}

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

	// Paced text injection (dosbox.type) state
	bool injecting_type                                      = false;
	std::deque<KeyStroke> pending_keys                       = {};
	std::chrono::steady_clock::time_point type_wall_deadline = {};

	// Wall-clock ceiling for a yielded wait/type so a stalled guest or
	// frame clock can't hang the script. Enforced by DispatchFrame and
	// ReapStalledWaits.
	static constexpr auto DefaultWallCeiling = std::chrono::seconds(30);
	std::chrono::steady_clock::duration wall_ceiling = DefaultWallCeiling;

	void RegisterApi();
	void Cleanup();
	bool CheckWaitForText();

	// Drop queued type() keys and resume the script. Shared by the
	// completion path and the reaper.
	ScriptState FinishTypeInjection();

	// Inject as many queued keystrokes as currently fit in the keyboard
	// buffer, keeping each stroke's press/release together so a key is
	// never held across frames. Returns true once the queue is empty.
	bool DrainPendingKeys();

	// Resume the coroutine with nargs results already on its stack and fold
	// the resume status into state/error_msg.
	ScriptState ResumeWith(int nargs);

	static int LuaWaitFrames(lua_State* L);
	static int LuaFrame(lua_State* L);
};

} // namespace Lua

#endif
