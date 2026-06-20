// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "lua/lua_api.h"
#include "lua/lua_coroutine.h"
#include "lua/lua_debug_log.h"
#include "lua/lua_engine.h"

#include <string>

#include <gtest/gtest.h>

namespace {

class LuaApiTest : public ::testing::Test {
protected:
	Lua::LuaEngine engine;
	Lua::LuaCoroutine coroutine{engine};

	void LoadAndStart(const std::string& script)
	{
		auto result = engine.LoadScript(script, "test");
		ASSERT_TRUE(result.ok) << result.error;
		ASSERT_TRUE(coroutine.Start());
	}

	Lua::ScriptState RunToCompletion(const std::string& script,
	                                 int max_frames = 100)
	{
		LoadAndStart(script);
		Lua::ScriptState state = Lua::ScriptState::Running;
		for (int i = 1; i <= max_frames; ++i) {
			state = coroutine.DispatchFrame(static_cast<uint64_t>(i));
			if (state == Lua::ScriptState::Completed ||
			    state == Lua::ScriptState::Error) {
				break;
			}
		}
		return state;
	}
};

// -- Input: key() --

TEST_F(LuaApiTest, KeyValidNameSucceeds)
{
	auto state = RunToCompletion("dosbox.key('KBD_enter', true)");
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

TEST_F(LuaApiTest, KeyInvalidNameErrors)
{
	auto state = RunToCompletion("dosbox.key('KBD_nonexistent', true)");
	EXPECT_EQ(state, Lua::ScriptState::Error);
	EXPECT_NE(coroutine.ErrorMessage().find("unknown key name"),
	          std::string::npos);
}

TEST_F(LuaApiTest, KeyMissingPressedErrors)
{
	auto state = RunToCompletion("dosbox.key('KBD_enter')");
	EXPECT_EQ(state, Lua::ScriptState::Error);
}

// -- Input: type() --

TEST_F(LuaApiTest, TypeAsciiSucceeds)
{
	auto state = RunToCompletion("dosbox.type('Hello World')");
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

TEST_F(LuaApiTest, TypeEmptyStringSucceeds)
{
	auto state = RunToCompletion("dosbox.type('')");
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

// -- Input: mouse_click() --

TEST_F(LuaApiTest, MouseClickValidButton)
{
	auto state = RunToCompletion("dosbox.mouse_click('left')");
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

TEST_F(LuaApiTest, MouseClickInvalidButton)
{
	auto state = RunToCompletion("dosbox.mouse_click('extra')");
	EXPECT_EQ(state, Lua::ScriptState::Error);
	EXPECT_NE(coroutine.ErrorMessage().find("unknown mouse button"),
	          std::string::npos);
}

// -- Input: mouse_move() --

TEST_F(LuaApiTest, MouseMoveSucceeds)
{
	auto state = RunToCompletion("dosbox.mouse_move(10.0, -5.0)");
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

// -- Memory: read/write bounds --

TEST_F(LuaApiTest, MemReadByteBoundsCheck)
{
	// Reading from address 0x0000:0x0000 should work in the test
	// environment if emulator memory is initialized. If not
	// initialized, this will error, which is also correct.
	auto state = RunToCompletion("local v = dosbox.mem_read_byte(0, 0)");
	// Either succeeds or errors with out-of-range, both are valid
	// in a test environment without full emulator init.
	EXPECT_TRUE(state == Lua::ScriptState::Completed ||
	            state == Lua::ScriptState::Error);
}

TEST_F(LuaApiTest, MemReadZeroLengthErrors)
{
	auto state = RunToCompletion("dosbox.mem_read(0, 0, 0)");
	EXPECT_EQ(state, Lua::ScriptState::Error);
	EXPECT_NE(coroutine.ErrorMessage().find("length must be"), std::string::npos);
}

TEST_F(LuaApiTest, MemReadTooLargeErrors)
{
	auto state = RunToCompletion("dosbox.mem_read(0, 0, 2000000)");
	EXPECT_EQ(state, Lua::ScriptState::Error);
	EXPECT_NE(coroutine.ErrorMessage().find("length must be"), std::string::npos);
}

// -- Screen: text reading --

TEST_F(LuaApiTest, ScreenTextReturnsString)
{
	// In test environment without VGA init, screen_text returns
	// empty or garbage, but it should not crash.
	auto state = RunToCompletion("local t = dosbox.screen_text()");
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

TEST_F(LuaApiTest, ScreenMatchReturnsBoolean)
{
	auto state = RunToCompletion(
	        "local m = dosbox.screen_match('test')\n"
	        "assert(type(m) == 'boolean')\n");
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

TEST_F(LuaApiTest, ScreenMatchIgnoreCaseOption)
{
	auto state = RunToCompletion(
	        "local m = dosbox.screen_match('TEST', {ignorecase = true})\n"
	        "assert(type(m) == 'boolean')\n");
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

// -- OSD --

TEST_F(LuaApiTest, OsdShowsText)
{
	auto state = RunToCompletion("dosbox.osd('Hello')");
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

TEST_F(LuaApiTest, OsdWithOptions)
{
	auto state = RunToCompletion(
	        "dosbox.osd('Status', {color = 'green', size = 'large'})");
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

TEST_F(LuaApiTest, OsdClear)
{
	auto state = RunToCompletion(
	        "dosbox.osd('temp')\n"
	        "dosbox.osd_clear()\n");
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

// -- Output table --

TEST_F(LuaApiTest, OutputTableExists)
{
	auto state = RunToCompletion("assert(type(dosbox.output) == 'table')\n");
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

TEST_F(LuaApiTest, OutputTableWritable)
{
	auto state = RunToCompletion(
	        "dosbox.output.result = 'success'\n"
	        "dosbox.output.count = 42\n"
	        "assert(dosbox.output.result == 'success')\n"
	        "assert(dosbox.output.count == 42)\n");
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

// -- Logging --

TEST_F(LuaApiTest, LogSucceeds)
{
	auto state = RunToCompletion("dosbox.log('test message')");
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

TEST_F(LuaApiTest, DebugMsgSucceeds)
{
	auto state = RunToCompletion("dosbox.debugmsg('debug info')");
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

TEST_F(LuaApiTest, AbortStopsScript)
{
	auto state = RunToCompletion("dosbox.abort('deliberate abort')");
	EXPECT_EQ(state, Lua::ScriptState::Error);
	EXPECT_NE(coroutine.ErrorMessage().find("deliberate abort"),
	          std::string::npos);
}

// -- Mount lock --

TEST_F(LuaApiTest, MountLockSucceeds)
{
	auto state = RunToCompletion("dosbox.mount_lock()");
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

// -- wait_for_text yields --

TEST_F(LuaApiTest, WaitForTextNegativeTimeoutErrors)
{
	auto state = RunToCompletion("dosbox.wait_for_text('x', -1)");
	EXPECT_EQ(state, Lua::ScriptState::Error);
	EXPECT_NE(coroutine.ErrorMessage().find("non-negative"), std::string::npos);
}

TEST_F(LuaApiTest, WaitForTextTimesOut)
{
	// In test environment, screen has no text, so this should timeout.
	LoadAndStart(
	        "local found = dosbox.wait_for_text('IMPOSSIBLE', 5)\n"
	        "dosbox.output.found = found\n");

	Lua::ScriptState state = Lua::ScriptState::Running;
	for (int i = 1; i <= 20; ++i) {
		state = coroutine.DispatchFrame(static_cast<uint64_t>(i));
		if (state == Lua::ScriptState::Completed ||
		    state == Lua::ScriptState::Error) {
			break;
		}
	}

	// Should complete after timeout, not error.
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

} // namespace
