// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_LUA_ENGINE_H
#define DOSBOX_LUA_ENGINE_H

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
	static constexpr size_t DefaultMemoryCapBytes = 16 * 1024 * 1024;

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

	int64_t InstructionLimit() const
	{
		return instruction_limit;
	}

	// Used by the instruction count hook callback (free function in
	// the .cpp file, not a member). Returns true if the limit was
	// exceeded and the script should be aborted.
	bool AddInstructions(int count);

private:
	struct AllocState {
		size_t current = 0;
		size_t peak    = 0;
		size_t cap     = DefaultMemoryCapBytes;
	};

	static void* LuaAllocator(void* ud, void* ptr, size_t osize, size_t nsize);

	AllocState alloc_state        = {};
	lua_State* state              = nullptr;
	int64_t instruction_limit     = 0;
	int64_t instructions_executed = 0;

	void CreateState();
	void DestroyState();
};

} // namespace Lua

#endif
