// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "lua/lua_engine.h"

#include <cstdlib>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

namespace {

constexpr int HookInterval        = 1000;
constexpr const char* RegistryKey = "LuaEngine";

} // namespace

namespace Lua {

LuaEngine::LuaEngine(const size_t memory_cap)
{
	alloc_state.cap = memory_cap;
	CreateState();
	SetInstructionLimit(DefaultInstructionLimit);
}

LuaEngine::~LuaEngine()
{
	DestroyState();
}

void LuaEngine::CreateState()
{
#if LUA_VERSION_NUM >= 505
	state = lua_newstate(LuaAllocator, &alloc_state, 0);
#else
	state = lua_newstate(LuaAllocator, &alloc_state);
#endif
	if (!state) {
		return;
	}

	// Open only the safe subset of standard libraries.
	// os, io, debug, package are deliberately excluded.
	luaL_requiref(state, "_G", luaopen_base, 1);
	lua_pop(state, 1);
	luaL_requiref(state, LUA_TABLIBNAME, luaopen_table, 1);
	lua_pop(state, 1);
	luaL_requiref(state, LUA_STRLIBNAME, luaopen_string, 1);
	lua_pop(state, 1);
	luaL_requiref(state, LUA_MATHLIBNAME, luaopen_math, 1);
	lua_pop(state, 1);
	luaL_requiref(state, LUA_COLIBNAME, luaopen_coroutine, 1);
	lua_pop(state, 1);
	luaL_requiref(state, LUA_UTF8LIBNAME, luaopen_utf8, 1);
	lua_pop(state, 1);

	// Remove dangerous functions from the base library.
	// These allow loading arbitrary code or bytecode.
	// String metatable escape: getmetatable('') can reach the string
	// library's metatable, which in an insufficiently isolated sandbox
	// could lead to _G. Block it entirely.
	const char* blocked[] = {
	        "dofile", "loadfile", "load", "require", "getmetatable", nullptr};
	for (const char** fn = blocked; *fn; ++fn) {
		lua_pushnil(state);
		lua_setglobal(state, *fn);
	}
}

void LuaEngine::DestroyState()
{
	if (state) {
		lua_close(state);
		state = nullptr;
	}
}

void* LuaEngine::LuaAllocator(void* ud, void* ptr, size_t osize, size_t nsize)
{
	auto* as = static_cast<AllocState*>(ud);

	if (nsize == 0) {
		as->current -= osize;
		std::free(ptr);
		return nullptr;
	}

	if (ptr == nullptr) {
		if (as->current + nsize > as->cap) {
			return nullptr;
		}
		void* block = std::malloc(nsize);
		if (block) {
			as->current += nsize;
			if (as->current > as->peak) {
				as->peak = as->current;
			}
		}
		return block;
	}

	// Reallocation.
	const auto delta = nsize > osize ? nsize - osize : 0;
	if (delta > 0 && as->current + delta > as->cap) {
		return nullptr;
	}

	void* block = std::realloc(ptr, nsize);
	if (block) {
		as->current = as->current - osize + nsize;
		if (as->current > as->peak) {
			as->peak = as->current;
		}
	}
	return block;
}

static void InstructionHook(lua_State* L, [[maybe_unused]] lua_Debug* ar)
{
	lua_getfield(L, LUA_REGISTRYINDEX, RegistryKey);
	auto* engine = static_cast<LuaEngine*>(lua_touserdata(L, -1));
	lua_pop(L, 1);

	if (!engine) {
		return;
	}

	const auto* reason = engine->CheckLimits(HookInterval);
	if (reason) {
		luaL_error(L, "%s", reason);
	}
}

void LuaEngine::Reset()
{
	DestroyState();

	alloc_state.current   = 0;
	alloc_state.peak      = 0;
	instructions_executed = 0;

	CreateState();

	if (state && instruction_limit > 0) {
		SetInstructionLimit(instruction_limit);
	}
}

const char* LuaEngine::CheckLimits(const int count)
{
	instructions_executed += count;
	if (instruction_limit > 0 && instructions_executed > instruction_limit) {
		return "instruction limit exceeded";
	}

	if (wall_clock_limit_ms > 0) {
		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
		        Clock::now() - exec_start);
		if (elapsed.count() > wall_clock_limit_ms) {
			return "wall-clock time limit exceeded";
		}
	}

	return nullptr;
}

void LuaEngine::SetWallClockLimit(const int64_t max_ms)
{
	wall_clock_limit_ms = max_ms;
}

void LuaEngine::SetInstructionLimit(const int64_t max_instructions)
{
	instruction_limit     = max_instructions;
	instructions_executed = 0;

	if (!state) {
		return;
	}

	if (max_instructions > 0) {
		lua_pushlightuserdata(state, this);
		lua_setfield(state, LUA_REGISTRYINDEX, RegistryKey);
		lua_sethook(state, InstructionHook, LUA_MASKCOUNT, HookInterval);
	} else {
		lua_sethook(state, nullptr, 0, 0);
	}
}

ScriptResult LuaEngine::LoadScript(const std::string& source, const std::string& name)
{
	if (!state) {
		return {false, "Lua state not initialized"};
	}

	instructions_executed = 0;
	exec_start            = Clock::now();

	// "t" flag: reject bytecode, accept only text source.
	const auto status = luaL_loadbufferx(
	        state, source.data(), source.size(), name.c_str(), "t");
	if (status != LUA_OK) {
		ScriptResult result = {false, lua_tostring(state, -1)};
		lua_pop(state, 1);
		return result;
	}

	const auto exec_status = lua_pcall(state, 0, 0, 0);
	if (exec_status != LUA_OK) {
		ScriptResult result = {false, lua_tostring(state, -1)};
		lua_pop(state, 1);
		return result;
	}

	return {true, {}};
}

} // namespace Lua
