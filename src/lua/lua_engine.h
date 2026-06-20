// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_LUA_ENGINE_H
#define DOSBOX_LUA_ENGINE_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

struct lua_State;

namespace Lua {

struct ScriptResult {
	bool ok           = false;
	std::string error = {};
};

class LuaEngine {
public:
	static constexpr size_t DefaultMemoryCapBytes    = 16 * 1024 * 1024;
	static constexpr int64_t DefaultInstructionLimit = 1000000;
	static constexpr int64_t DefaultWallClockLimitMs = 5000;

	explicit LuaEngine(size_t memory_cap = DefaultMemoryCapBytes);
	~LuaEngine();

	LuaEngine(const LuaEngine&)            = delete;
	LuaEngine& operator=(const LuaEngine&) = delete;

	ScriptResult LoadScript(const std::string& source, const std::string& name);

	size_t MemoryUsage() const
	{
		return alloc_state.current;
	}
	size_t MemoryCap() const
	{
		return alloc_state.cap;
	}

	void Reset();
	void SetInstructionLimit(int64_t max_instructions);
	void SetWallClockLimit(int64_t max_ms);

	int64_t InstructionLimit() const
	{
		return instruction_limit;
	}

	lua_State* GetState() const
	{
		return state;
	}

	bool HasLoadedScript() const
	{
		return has_loaded_script;
	}

	// Load and immediately execute a script. Used by tests and
	// non-coroutine callers.
	ScriptResult RunScript(const std::string& source, const std::string& name);

	// Used by the instruction count hook callback (free function in
	// the .cpp file, not a member). Returns nullptr if ok, or an
	// error message if a limit was exceeded.
	const char* CheckLimits(int instruction_count);

private:
	struct AllocState {
		size_t current = 0;
		size_t peak    = 0;
		size_t cap     = DefaultMemoryCapBytes;
	};

	static void* LuaAllocator(void* ud, void* ptr, size_t osize, size_t nsize);

	using Clock = std::chrono::steady_clock;

	AllocState alloc_state        = {};
	lua_State* state              = nullptr;
	int64_t instruction_limit     = 0;
	int64_t instructions_executed = 0;
	int64_t wall_clock_limit_ms   = DefaultWallClockLimitMs;
	Clock::time_point exec_start  = {};
	bool has_loaded_script        = false;

	void CreateState();
	void DestroyState();
};

} // namespace Lua

#endif
