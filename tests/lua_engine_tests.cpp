// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "lua/lua_engine.h"

#include <string>

#include <gtest/gtest.h>

extern "C" {
#include <lua.h>
}

namespace {

TEST(LuaEngine, ConstructsAndDestroys)
{
	auto engine = Lua::LuaEngine();
}

TEST(LuaEngine, MemoryCapEnforced)
{
	auto engine = Lua::LuaEngine();

	// A script that tries to allocate more than the 16 MB cap.
	const auto result = engine.RunScript("local s = string.rep('x', 32 * 1024 * 1024)",
	                                      "oom-test");
	EXPECT_FALSE(result.ok);
	EXPECT_NE(result.error.find("memory"), std::string::npos);
}

TEST(LuaEngine, MemoryUsageTracked)
{
	auto engine = Lua::LuaEngine();

	const auto baseline = engine.MemoryUsage();
	EXPECT_GT(baseline, 0u);

	auto result = engine.RunScript("local t = {} for i=1,10000 do t[i] = i end",
	                                "alloc-test");
	EXPECT_TRUE(result.ok);
	EXPECT_GT(engine.MemoryUsage(), baseline);
}

// -- Sandbox: blocked modules and functions --

TEST(LuaEngine, SandboxBlocksOsModule)
{
	auto engine = Lua::LuaEngine();
	const auto result = engine.RunScript("os.execute('echo pwned')", "os-escape");
	EXPECT_FALSE(result.ok);
}

TEST(LuaEngine, SandboxBlocksIoModule)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript("io.open('/etc/passwd', 'r')",
	                                      "io-escape");
	EXPECT_FALSE(result.ok);
}

TEST(LuaEngine, SandboxBlocksDebugModule)
{
	auto engine = Lua::LuaEngine();
	const auto result = engine.RunScript("debug.getinfo(1)", "debug-escape");
	EXPECT_FALSE(result.ok);
}

TEST(LuaEngine, SandboxBlocksPackageModule)
{
	auto engine = Lua::LuaEngine();
	const auto result = engine.RunScript("package.path = '.'", "package-escape");
	EXPECT_FALSE(result.ok);
}

TEST(LuaEngine, SandboxBlocksRequire)
{
	auto engine = Lua::LuaEngine();
	const auto result = engine.RunScript("require('os')", "require-escape");
	EXPECT_FALSE(result.ok);
}

TEST(LuaEngine, SandboxBlocksDofile)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript("dofile('/etc/passwd')",
	                                      "dofile-escape");
	EXPECT_FALSE(result.ok);
}

TEST(LuaEngine, SandboxBlocksLoadfile)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript("loadfile('/etc/passwd')()",
	                                      "loadfile-escape");
	EXPECT_FALSE(result.ok);
}

TEST(LuaEngine, SandboxBlocksLoad)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript("load('os.execute(\"echo\")')",
	                                      "load-escape");
	EXPECT_FALSE(result.ok);
}

// -- Sandbox: allowed libraries --

TEST(LuaEngine, SandboxAllowsStringLib)
{
	auto engine = Lua::LuaEngine();
	const auto result = engine.RunScript("local s = string.format('%d', 42)",
	                                      "string-ok");
	EXPECT_TRUE(result.ok);
}

TEST(LuaEngine, SandboxAllowsTableLib)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript("local t = {3,1,2} table.sort(t)",
	                                      "table-ok");
	EXPECT_TRUE(result.ok);
}

TEST(LuaEngine, SandboxAllowsMathLib)
{
	auto engine = Lua::LuaEngine();
	const auto result = engine.RunScript("local x = math.floor(3.14)", "math-ok");
	EXPECT_TRUE(result.ok);
}

TEST(LuaEngine, SandboxAllowsCoroutineLib)
{
	auto engine = Lua::LuaEngine();
	const auto result = engine.RunScript("local co = coroutine.create(function() end)",
	                                      "coroutine-ok");
	EXPECT_TRUE(result.ok);
}

TEST(LuaEngine, SandboxAllowsBaseFunctions)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript(
	        "local n = tonumber('42')\n"
	        "local s = tostring(42)\n"
	        "local t = type(42)\n"
	        "local ok, err = pcall(function() error('test') end)\n"
	        "assert(not ok)\n",
	        "base-ok");
	EXPECT_TRUE(result.ok);
}

// -- Bytecode rejection --

TEST(LuaEngine, RejectsBytecode)
{
	auto engine = Lua::LuaEngine();

	// Lua bytecode header starts with \x1bLua
	const std::string bytecode = "\x1b\x4c\x75\x61\x54\x00\x00\x00";
	const auto result = engine.RunScript(bytecode, "bytecode-attack");
	EXPECT_FALSE(result.ok);
}

// -- Instruction limit --

TEST(LuaEngine, DefaultInstructionLimitStopsInfiniteLoop)
{
	auto engine = Lua::LuaEngine();

	// No explicit SetInstructionLimit - the default must stop this.
	const auto result = engine.RunScript("while true do end",
	                                      "default-limit-loop");
	EXPECT_FALSE(result.ok);
	EXPECT_NE(result.error.find("instruction"), std::string::npos);
}

TEST(LuaEngine, InstructionLimitStopsInfiniteLoop)
{
	auto engine = Lua::LuaEngine();
	engine.SetInstructionLimit(10000);

	const auto result = engine.RunScript("while true do end", "infinite-loop");
	EXPECT_FALSE(result.ok);
	EXPECT_NE(result.error.find("instruction"), std::string::npos);
}

TEST(LuaEngine, InstructionLimitAllowsShortScripts)
{
	auto engine = Lua::LuaEngine();
	engine.SetInstructionLimit(100000);

	const auto result = engine.RunScript(
	        "local sum = 0\n"
	        "for i = 1, 1000 do sum = sum + i end\n",
	        "short-script");
	EXPECT_TRUE(result.ok);
}

// -- Reset and sequential script isolation --

TEST(LuaEngine, ResetClearsScriptState)
{
	auto engine = Lua::LuaEngine();

	auto result = engine.RunScript("my_global = 42", "set-global");
	EXPECT_TRUE(result.ok);

	engine.Reset();

	result = engine.RunScript("assert(my_global == nil, 'leaked: ' .. tostring(my_global))",
	                           "check-leak");
	EXPECT_TRUE(result.ok) << result.error;
}

TEST(LuaEngine, ResetKeepsSandbox)
{
	auto engine = Lua::LuaEngine();
	engine.Reset();

	auto result = engine.RunScript("os.execute('echo pwned')",
	                                "post-reset-escape");
	EXPECT_FALSE(result.ok);
}

TEST(LuaEngine, SequentialScriptsIsolated)
{
	auto engine = Lua::LuaEngine();

	auto r1 = engine.RunScript("shared = 'first'", "script-1");
	EXPECT_TRUE(r1.ok);

	engine.Reset();

	auto r2 = engine.RunScript("assert(shared == nil, 'leaked from script-1')",
	                            "script-2");
	EXPECT_TRUE(r2.ok) << r2.error;
}

TEST(LuaEngine, ResetPreservesInstructionLimit)
{
	auto engine = Lua::LuaEngine();
	engine.SetInstructionLimit(10000);

	engine.Reset();

	const auto result = engine.RunScript("while true do end", "post-reset-loop");
	EXPECT_FALSE(result.ok);
	EXPECT_NE(result.error.find("instruction"), std::string::npos);
}

// -- Pattern function length guard --

TEST(LuaEngine, PatternFindAllowsShortStrings)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript(
	        "local pos = string.find('hello world', 'world')\n"
	        "assert(pos == 7)\n",
	        "find-short");
	EXPECT_TRUE(result.ok) << result.error;
}

TEST(LuaEngine, PatternFindRejectsOversizedSubject)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript(
	        "local s = string.rep('a', 65 * 1024)\n"
	        "string.find(s, 'b')\n",
	        "find-oversize");
	EXPECT_FALSE(result.ok);
	EXPECT_NE(result.error.find("too long"), std::string::npos);
}

TEST(LuaEngine, PatternMatchRejectsOversizedSubject)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript(
	        "local s = string.rep('a', 65 * 1024)\n"
	        "string.match(s, 'b')\n",
	        "match-oversize");
	EXPECT_FALSE(result.ok);
	EXPECT_NE(result.error.find("too long"), std::string::npos);
}

TEST(LuaEngine, PatternGsubRejectsOversizedSubject)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript(
	        "local s = string.rep('a', 65 * 1024)\n"
	        "string.gsub(s, 'a', 'b')\n",
	        "gsub-oversize");
	EXPECT_FALSE(result.ok);
	EXPECT_NE(result.error.find("too long"), std::string::npos);
}

TEST(LuaEngine, PatternFindAllowsAtLimit)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript(
	        "local s = string.rep('a', 64 * 1024)\n"
	        "string.find(s, 'b')\n",
	        "find-at-limit");
	EXPECT_TRUE(result.ok) << result.error;
}

// -- Pattern complexity guard --

TEST(LuaEngine, PatternRejectsCatastrophicPattern)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript(
	        "local s = string.rep('a', 40)\n"
	        "local p = string.rep('.-', 8) .. 'z'\n"
	        "return string.find(s, p)\n",
	        "redos");
	EXPECT_FALSE(result.ok);
	EXPECT_NE(result.error.find("too complex"), std::string::npos);
}

TEST(LuaEngine, PatternRejectsRuntimeBuiltComplexPattern)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript(
	        "local s = string.rep('a', 200)\n"
	        "local p = string.rep('.-', 12) .. 'z'\n"
	        "return string.match(s, p)\n",
	        "redos-runtime");
	EXPECT_FALSE(result.ok);
	EXPECT_NE(result.error.find("too complex"), std::string::npos);
}

TEST(LuaEngine, PatternAllowsSimpleOnLargeSubject)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript(
	        "local s = string.rep('a', 60 * 1024) .. 'b'\n"
	        "return (string.find(s, 'b') ~= nil)\n",
	        "simple-large");
	EXPECT_TRUE(result.ok) << result.error;
}

TEST(LuaEngine, PatternAllowsComplexOnSmallSubject)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript(
	        "local s = 'Drive C: ready  Insert disk 2'\n"
	        "return string.match(s, 'disk%%s*(%%d+)')\n",
	        "screen-text");
	EXPECT_TRUE(result.ok) << result.error;
}

TEST(LuaEngine, PatternQuantifierInsideSetIsLiteral)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript(
	        "local s = string.rep('x', 60 * 1024)\n"
	        "return (string.find(s, '[*+-]') == nil)\n",
	        "set-literal");
	EXPECT_TRUE(result.ok) << result.error;
}

TEST(LuaEngine, PatternEscapedQuantifierIsLiteral)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript(
	        "local s = string.rep('a', 60 * 1024) .. '*'\n"
	        "return (string.find(s, '%%*') ~= nil)\n",
	        "escaped-literal");
	EXPECT_TRUE(result.ok) << result.error;
}

// -- Metatable escape --

TEST(LuaEngine, SandboxBlocksGetmetatable)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript("local mt = getmetatable('')\n",
	                                      "metatable-escape");
	EXPECT_FALSE(result.ok);
}

// S1: setmetatable allows __gc, __index, __tostring injection
TEST(LuaEngine, SandboxBlocksSetmetatable)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript(
	        "local t = {}\n"
	        "setmetatable(t, {__tostring = function() return 'injected' end})\n",
	        "setmetatable-escape");
	EXPECT_FALSE(result.ok);
}

// S2: string.dump leaks bytecode representation
TEST(LuaEngine, SandboxBlocksStringDump)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript(
	        "local bc = string.dump(function() return 1 end)\n",
	        "string-dump-escape");
	EXPECT_FALSE(result.ok);
}

// S3: collectgarbage does unbounded C-side work, bypasses instruction hook
TEST(LuaEngine, SandboxBlocksCollectgarbage)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript("collectgarbage('count')\n",
	                                      "collectgarbage-escape");
	EXPECT_FALSE(result.ok);
}

// S4: raw* functions bypass metamethods
TEST(LuaEngine, SandboxBlocksRawset)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript("rawset({}, 'k', 'v')\n",
	                                      "rawset-escape");
	EXPECT_FALSE(result.ok);
}

TEST(LuaEngine, SandboxBlocksRawget)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript("rawget({}, 'k')\n",
	                                      "rawget-escape");
	EXPECT_FALSE(result.ok);
}

TEST(LuaEngine, SandboxBlocksRawlen)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript("rawlen({})\n",
	                                      "rawlen-escape");
	EXPECT_FALSE(result.ok);
}

TEST(LuaEngine, SandboxBlocksRawequal)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript("rawequal({}, {})\n",
	                                      "rawequal-escape");
	EXPECT_FALSE(result.ok);
}

// S5: timers must reset at start, not just at load
TEST(LuaEngine, ResetTimersClearsCounters)
{
	auto engine = Lua::LuaEngine();
	engine.SetInstructionLimit(50000);

	// Burn through most of the instruction budget.
	auto r1 = engine.RunScript(
	        "local sum = 0 for i = 1, 10000 do sum = sum + i end",
	        "burn-budget");
	EXPECT_TRUE(r1.ok) << r1.error;

	// Without ResetTimers, a second run would start with the
	// accumulated count and might exceed the limit.
	engine.ResetTimers();

	auto r2 = engine.RunScript(
	        "local sum = 0 for i = 1, 10000 do sum = sum + i end",
	        "after-reset");
	EXPECT_TRUE(r2.ok) << r2.error;
}

// A1: non-string error objects must not crash
TEST(LuaEngine, NonStringErrorTableDoesNotCrash)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript("error({})", "table-error");
	EXPECT_FALSE(result.ok);
	EXPECT_NE(result.error.find("non-string error"), std::string::npos);
}

TEST(LuaEngine, NonStringErrorNilDoesNotCrash)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript("error(nil)", "nil-error");
	EXPECT_FALSE(result.ok);
}

TEST(LuaEngine, NonStringErrorBoolDoesNotCrash)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.RunScript("error(true)", "bool-error");
	EXPECT_FALSE(result.ok);
	EXPECT_NE(result.error.find("non-string error"), std::string::npos);
}

// D1: seed parameter produces deterministic random output
TEST(LuaEngine, SeedRandomProducesDeterministicOutput)
{
	auto run_seeded = [](int64_t seed) -> std::string {
		auto engine = Lua::LuaEngine();
		engine.SeedRandom(seed);
		auto result = engine.RunScript(
		        "result = ''\n"
		        "for i = 1, 5 do\n"
		        "  result = result .. tostring(math.random(1, 1000)) .. ','\n"
		        "end\n",
		        "seed-test");
		if (!result.ok) {
			return "ERROR: " + result.error;
		}

		auto* L = engine.GetState();
		lua_getglobal(L, "result");
		const char* s = lua_tostring(L, -1);
		std::string out = s ? s : "";
		lua_pop(L, 1);
		return out;
	};

	// Same seed must produce same sequence.
	const auto seq1 = run_seeded(42);
	const auto seq2 = run_seeded(42);
	EXPECT_EQ(seq1, seq2);
	EXPECT_FALSE(seq1.empty());

	// Different seed must produce different sequence.
	const auto seq3 = run_seeded(99);
	EXPECT_NE(seq1, seq3);
}

} // namespace
