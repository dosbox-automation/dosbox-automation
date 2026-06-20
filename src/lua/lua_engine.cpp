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

// Cap on subject string length for pattern-matching functions.
// Prevents pathological backtracking in the C pattern matcher
// from stalling the emulation thread. The instruction hook and
// wall-clock watchdog can't preempt a single C library call.
constexpr size_t MaxPatternSubjectLen = 64 * 1024;

// Checks that argument 1 (the subject string) does not exceed the
// cap. Called from the guarded wrappers below.
static void CheckSubjectLength(lua_State* L)
{
	size_t len = 0;
	luaL_checklstring(L, 1, &len);
	if (len > MaxPatternSubjectLen) {
		luaL_error(L,
		           "string too long for pattern operation (%d bytes, limit %d)",
		           static_cast<int>(len),
		           static_cast<int>(MaxPatternSubjectLen));
	}
}

// Wraps a string library function with a length check on argument 1.
// Stores the original function as an upvalue, checks length, then
// tail-calls the original.
static int GuardedPatternFunc(lua_State* L)
{
	CheckSubjectLength(L);
	lua_pushvalue(L, lua_upvalueindex(1));
	lua_insert(L, 1);
	lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
	return lua_gettop(L);
}

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

	// Wrap pattern-matching functions with a length guard on the
	// subject string. The Lua pattern matcher runs entirely in C,
	// invisible to both the instruction hook and the wall-clock
	// watchdog, so pathological patterns on large strings stall
	// the emulation thread without any limit firing. Capping the
	// subject length is the only in-process defense.
	const char* pattern_funcs[] = {"find", "match", "gmatch", "gsub", nullptr};
	for (const char** fn = pattern_funcs; *fn; ++fn) {
		lua_getfield(state, -1, *fn);
		lua_pushcclosure(state, GuardedPatternFunc, 1);
		lua_setfield(state, -2, *fn);
	}

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
