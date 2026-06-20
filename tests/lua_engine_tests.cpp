// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "lua/lua_engine.h"

#include <string>

#include <gtest/gtest.h>

namespace {

TEST(LuaEngine, ConstructsAndDestroys)
{
	auto engine = Lua::LuaEngine();
}

TEST(LuaEngine, MemoryCapEnforced)
{
	auto engine = Lua::LuaEngine();

	// A script that tries to allocate more than the 16 MB cap.
	const auto result = engine.LoadScript("local s = string.rep('x', 32 * 1024 * 1024)",
	                                      "oom-test");
	EXPECT_FALSE(result.ok);
	EXPECT_NE(result.error.find("memory"), std::string::npos);
}

TEST(LuaEngine, MemoryUsageTracked)
{
	auto engine = Lua::LuaEngine();

	const auto baseline = engine.MemoryUsage();
	EXPECT_GT(baseline, 0u);

	auto result = engine.LoadScript("local t = {} for i=1,10000 do t[i] = i end",
	                                "alloc-test");
	EXPECT_TRUE(result.ok);
	EXPECT_GT(engine.MemoryUsage(), baseline);
}

// -- Sandbox: blocked modules and functions --

TEST(LuaEngine, SandboxBlocksOsModule)
{
	auto engine = Lua::LuaEngine();
	const auto result = engine.LoadScript("os.execute('echo pwned')", "os-escape");
	EXPECT_FALSE(result.ok);
}

TEST(LuaEngine, SandboxBlocksIoModule)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.LoadScript("io.open('/etc/passwd', 'r')",
	                                      "io-escape");
	EXPECT_FALSE(result.ok);
}

TEST(LuaEngine, SandboxBlocksDebugModule)
{
	auto engine = Lua::LuaEngine();
	const auto result = engine.LoadScript("debug.getinfo(1)", "debug-escape");
	EXPECT_FALSE(result.ok);
}

TEST(LuaEngine, SandboxBlocksPackageModule)
{
	auto engine = Lua::LuaEngine();
	const auto result = engine.LoadScript("package.path = '.'", "package-escape");
	EXPECT_FALSE(result.ok);
}

TEST(LuaEngine, SandboxBlocksRequire)
{
	auto engine = Lua::LuaEngine();
	const auto result = engine.LoadScript("require('os')", "require-escape");
	EXPECT_FALSE(result.ok);
}

TEST(LuaEngine, SandboxBlocksDofile)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.LoadScript("dofile('/etc/passwd')",
	                                      "dofile-escape");
	EXPECT_FALSE(result.ok);
}

TEST(LuaEngine, SandboxBlocksLoadfile)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.LoadScript("loadfile('/etc/passwd')()",
	                                      "loadfile-escape");
	EXPECT_FALSE(result.ok);
}

TEST(LuaEngine, SandboxBlocksLoad)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.LoadScript("load('os.execute(\"echo\")')",
	                                      "load-escape");
	EXPECT_FALSE(result.ok);
}

// -- Sandbox: allowed libraries --

TEST(LuaEngine, SandboxAllowsStringLib)
{
	auto engine = Lua::LuaEngine();
	const auto result = engine.LoadScript("local s = string.format('%d', 42)",
	                                      "string-ok");
	EXPECT_TRUE(result.ok);
}

TEST(LuaEngine, SandboxAllowsTableLib)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.LoadScript("local t = {3,1,2} table.sort(t)",
	                                      "table-ok");
	EXPECT_TRUE(result.ok);
}

TEST(LuaEngine, SandboxAllowsMathLib)
{
	auto engine = Lua::LuaEngine();
	const auto result = engine.LoadScript("local x = math.floor(3.14)", "math-ok");
	EXPECT_TRUE(result.ok);
}

TEST(LuaEngine, SandboxAllowsCoroutineLib)
{
	auto engine = Lua::LuaEngine();
	const auto result = engine.LoadScript("local co = coroutine.create(function() end)",
	                                      "coroutine-ok");
	EXPECT_TRUE(result.ok);
}

TEST(LuaEngine, SandboxAllowsBaseFunctions)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.LoadScript(
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
	const auto result = engine.LoadScript(bytecode, "bytecode-attack");
	EXPECT_FALSE(result.ok);
}

// -- Instruction limit --

TEST(LuaEngine, DefaultInstructionLimitStopsInfiniteLoop)
{
	auto engine = Lua::LuaEngine();

	// No explicit SetInstructionLimit - the default must stop this.
	const auto result = engine.LoadScript("while true do end",
	                                      "default-limit-loop");
	EXPECT_FALSE(result.ok);
	EXPECT_NE(result.error.find("instruction"), std::string::npos);
}

TEST(LuaEngine, InstructionLimitStopsInfiniteLoop)
{
	auto engine = Lua::LuaEngine();
	engine.SetInstructionLimit(10000);

	const auto result = engine.LoadScript("while true do end", "infinite-loop");
	EXPECT_FALSE(result.ok);
	EXPECT_NE(result.error.find("instruction"), std::string::npos);
}

TEST(LuaEngine, InstructionLimitAllowsShortScripts)
{
	auto engine = Lua::LuaEngine();
	engine.SetInstructionLimit(100000);

	const auto result = engine.LoadScript(
	        "local sum = 0\n"
	        "for i = 1, 1000 do sum = sum + i end\n",
	        "short-script");
	EXPECT_TRUE(result.ok);
}

// -- Reset and sequential script isolation --

TEST(LuaEngine, ResetClearsScriptState)
{
	auto engine = Lua::LuaEngine();

	auto result = engine.LoadScript("my_global = 42", "set-global");
	EXPECT_TRUE(result.ok);

	engine.Reset();

	result = engine.LoadScript("assert(my_global == nil, 'leaked: ' .. tostring(my_global))",
	                           "check-leak");
	EXPECT_TRUE(result.ok) << result.error;
}

TEST(LuaEngine, ResetKeepsSandbox)
{
	auto engine = Lua::LuaEngine();
	engine.Reset();

	auto result = engine.LoadScript("os.execute('echo pwned')",
	                                "post-reset-escape");
	EXPECT_FALSE(result.ok);
}

TEST(LuaEngine, SequentialScriptsIsolated)
{
	auto engine = Lua::LuaEngine();

	auto r1 = engine.LoadScript("shared = 'first'", "script-1");
	EXPECT_TRUE(r1.ok);

	engine.Reset();

	auto r2 = engine.LoadScript("assert(shared == nil, 'leaked from script-1')",
	                            "script-2");
	EXPECT_TRUE(r2.ok) << r2.error;
}

TEST(LuaEngine, ResetPreservesInstructionLimit)
{
	auto engine = Lua::LuaEngine();
	engine.SetInstructionLimit(10000);

	engine.Reset();

	const auto result = engine.LoadScript("while true do end", "post-reset-loop");
	EXPECT_FALSE(result.ok);
	EXPECT_NE(result.error.find("instruction"), std::string::npos);
}

// -- Metatable escape --

TEST(LuaEngine, SandboxBlocksGetmetatable)
{
	auto engine       = Lua::LuaEngine();
	const auto result = engine.LoadScript("local mt = getmetatable('')\n",
	                                      "metatable-escape");
	EXPECT_FALSE(result.ok);
}

} // namespace
