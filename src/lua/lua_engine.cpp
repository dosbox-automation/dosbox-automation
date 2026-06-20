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

// Subject and pattern caps for the pattern-matching functions.
// The Lua pattern matcher runs entirely in one C call, which neither
// the instruction hook nor the wall-clock watchdog can preempt, so
// the cost has to be bounded before the call runs.
constexpr size_t MaxPatternSubjectLen = 64 * 1024;
constexpr size_t MaxPatternLen        = 1024;

// Worst-case backtracking cost is ~ subject_len ^ quantifiers. We
// bound it in bits: quantifiers * bit_length(subject_len). 24 bits
// is roughly 1.6e7 match steps, well under a second.
constexpr int MaxPatternCostBits = 24;

static int BitLength(size_t n)
{
	int bits = 0;
	while (n > 0) {
		++bits;
		n >>= 1;
	}
	return bits;
}

// Counts the unbounded quantifiers (* + -) that act as pattern
// operators. Escaped quantifiers (%* etc.) and any * + - inside a
// [set] are literals and are skipped. These are the only constructs
// that drive n^q backtracking; ? is bounded and ignored.
static int CountUnboundedQuantifiers(const char* p, size_t len)
{
	int count   = 0;
	bool in_set = false;

	for (size_t i = 0; i < len; ++i) {
		const char c = p[i];

		if (c == '%') {
			++i;
			continue;
		}

		if (in_set) {
			if (c == ']') {
				in_set = false;
			}
			continue;
		}

		if (c == '[') {
			in_set = true;
			if (i + 1 < len && p[i + 1] == '^') {
				++i;
			}
			if (i + 1 < len && p[i + 1] == ']') {
				++i;
			}
			continue;
		}

		if (c == '*' || c == '+' || c == '-') {
			++count;
		}
	}
	return count;
}

// Validates argument 1 (subject) and argument 2 (pattern) for all
// four pattern functions before the matcher runs.
static void CheckPatternComplexity(lua_State* L)
{
	size_t subject_len = 0;
	luaL_checklstring(L, 1, &subject_len);

	size_t pattern_len    = 0;
	const char* pattern_p = luaL_checklstring(L, 2, &pattern_len);

	if (subject_len > MaxPatternSubjectLen) {
		luaL_error(L,
		           "string too long for pattern operation (%d bytes, limit %d)",
		           static_cast<int>(subject_len),
		           static_cast<int>(MaxPatternSubjectLen));
	}

	if (pattern_len > MaxPatternLen) {
		luaL_error(L,
		           "pattern too long (%d bytes, limit %d)",
		           static_cast<int>(pattern_len),
		           static_cast<int>(MaxPatternLen));
	}

	const int quantifiers = CountUnboundedQuantifiers(pattern_p, pattern_len);
	const long long cost_bits = static_cast<long long>(quantifiers) *
	                            BitLength(subject_len);
	if (cost_bits > MaxPatternCostBits) {
		luaL_error(L,
		           "pattern too complex for subject length "
		           "(%d quantifiers over %d bytes)",
		           quantifiers,
		           static_cast<int>(subject_len));
	}
}

// Wraps a string library function with the complexity check. Stores
// the original as an upvalue, checks, then tail-calls the original.
static int GuardedPatternFunc(lua_State* L)
{
	CheckPatternComplexity(L);
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
	has_loaded_script     = false;

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

	// Store the compiled chunk in the registry for coroutine use.
	lua_setfield(state, LUA_REGISTRYINDEX, "LoadedChunk");
	has_loaded_script = true;
	return {true, {}};
}

ScriptResult LuaEngine::RunScript(const std::string& source, const std::string& name)
{
	auto load_result = LoadScript(source, name);
	if (!load_result.ok) {
		return load_result;
	}

	lua_getfield(state, LUA_REGISTRYINDEX, "LoadedChunk");
	if (lua_isnil(state, -1)) {
		lua_pop(state, 1);
		return {false, "no script loaded"};
	}

	exec_start            = Clock::now();
	instructions_executed = 0;

	const auto exec_status = lua_pcall(state, 0, 0, 0);
	if (exec_status != LUA_OK) {
		ScriptResult result = {false, lua_tostring(state, -1)};
		lua_pop(state, 1);
		return result;
	}

	return {true, {}};
}

} // namespace Lua
