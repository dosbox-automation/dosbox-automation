// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "lua/lua_coroutine.h"
#include "lua/lua_engine.h"

#include <chrono>
#include <string>
#include <thread>

#include <gtest/gtest.h>

namespace {

class LuaCoroutineTest : public ::testing::Test {
protected:
	Lua::LuaEngine engine;
	Lua::LuaCoroutine coroutine{engine};
};

TEST_F(LuaCoroutineTest, StartsIdle)
{
	EXPECT_EQ(coroutine.State(), Lua::ScriptState::Idle);
}

TEST_F(LuaCoroutineTest, StartFailsWithoutLoadedScript)
{
	EXPECT_FALSE(coroutine.Start());
	EXPECT_EQ(coroutine.State(), Lua::ScriptState::Idle);
}

TEST_F(LuaCoroutineTest, StartSucceedsWithLoadedScript)
{
	auto result = engine.LoadScript("return 42", "test");
	ASSERT_TRUE(result.ok) << result.error;

	EXPECT_TRUE(coroutine.Start());
	EXPECT_EQ(coroutine.State(), Lua::ScriptState::Running);
}

TEST_F(LuaCoroutineTest, ScriptCompletesOnFirstDispatch)
{
	engine.LoadScript("return 42", "test");
	coroutine.Start();

	auto state = coroutine.DispatchFrame(1);
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

TEST_F(LuaCoroutineTest, WaitFramesYieldsCoroutine)
{
	engine.LoadScript("dosbox.wait_frames(3)", "wait-test");
	coroutine.Start();

	auto state = coroutine.DispatchFrame(1);
	EXPECT_EQ(state, Lua::ScriptState::Yielded);
}

TEST_F(LuaCoroutineTest, WaitFramesResumesAfterEnoughFrames)
{
	engine.LoadScript(
	        "dosbox.wait_frames(3)\n"
	        "return 1\n",
	        "wait-resume");
	coroutine.Start();

	EXPECT_EQ(coroutine.DispatchFrame(1), Lua::ScriptState::Yielded);
	EXPECT_EQ(coroutine.DispatchFrame(2), Lua::ScriptState::Yielded);
	EXPECT_EQ(coroutine.DispatchFrame(3), Lua::ScriptState::Yielded);
	EXPECT_EQ(coroutine.DispatchFrame(4), Lua::ScriptState::Completed);
}

TEST_F(LuaCoroutineTest, MultipleWaits)
{
	engine.LoadScript(
	        "dosbox.wait_frames(2)\n"
	        "dosbox.wait_frames(3)\n"
	        "return 1\n",
	        "multi-wait");
	coroutine.Start();

	// First wait: yield at frame 1, wait_until = 3
	EXPECT_EQ(coroutine.DispatchFrame(1), Lua::ScriptState::Yielded);
	EXPECT_EQ(coroutine.DispatchFrame(2), Lua::ScriptState::Yielded);
	// Frame 3: resumes, hits second wait_frames(3), yields again.
	// wait_until = 3 + 3 = 6
	EXPECT_EQ(coroutine.DispatchFrame(3), Lua::ScriptState::Yielded);
	EXPECT_EQ(coroutine.DispatchFrame(4), Lua::ScriptState::Yielded);
	EXPECT_EQ(coroutine.DispatchFrame(5), Lua::ScriptState::Yielded);
	EXPECT_EQ(coroutine.DispatchFrame(6), Lua::ScriptState::Completed);
}

TEST_F(LuaCoroutineTest, FrameReturnsCurrentFrame)
{
	engine.LoadScript(
	        "dosbox.wait_frames(1)\n"
	        "local f = dosbox.frame()\n"
	        "assert(f > 0, 'frame should be > 0, got ' .. tostring(f))\n",
	        "frame-test");
	coroutine.Start();

	coroutine.DispatchFrame(1);
	auto state = coroutine.DispatchFrame(2);
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

TEST_F(LuaCoroutineTest, ScriptErrorReportsMessage)
{
	engine.LoadScript("error('deliberate error')", "error-test");
	coroutine.Start();

	auto state = coroutine.DispatchFrame(1);
	EXPECT_EQ(state, Lua::ScriptState::Error);
	EXPECT_NE(coroutine.ErrorMessage().find("deliberate error"),
	          std::string::npos);
}

TEST_F(LuaCoroutineTest, StopAbortsRunningScript)
{
	engine.LoadScript("while true do dosbox.wait_frames(1) end", "stop-test");
	coroutine.Start();

	coroutine.DispatchFrame(1);
	EXPECT_EQ(coroutine.State(), Lua::ScriptState::Yielded);

	coroutine.Stop();
	EXPECT_EQ(coroutine.State(), Lua::ScriptState::Idle);
}

TEST_F(LuaCoroutineTest, DispatchFrameNoOpWhenIdle)
{
	auto state = coroutine.DispatchFrame(1);
	EXPECT_EQ(state, Lua::ScriptState::Idle);
}

TEST_F(LuaCoroutineTest, DispatchFrameNoOpWhenCompleted)
{
	engine.LoadScript("return 1", "done");
	coroutine.Start();
	coroutine.DispatchFrame(1);

	EXPECT_EQ(coroutine.State(), Lua::ScriptState::Completed);
	auto state = coroutine.DispatchFrame(2);
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

TEST_F(LuaCoroutineTest, WaitFramesZeroResumesNextFrame)
{
	engine.LoadScript(
	        "dosbox.wait_frames(0)\n"
	        "return 1\n",
	        "wait-zero");
	coroutine.Start();

	EXPECT_EQ(coroutine.DispatchFrame(1), Lua::ScriptState::Yielded);
	EXPECT_EQ(coroutine.DispatchFrame(2), Lua::ScriptState::Completed);
}

TEST_F(LuaCoroutineTest, WaitFramesNegativeIsError)
{
	engine.LoadScript("dosbox.wait_frames(-1)", "neg-wait");
	coroutine.Start();

	auto state = coroutine.DispatchFrame(1);
	EXPECT_EQ(state, Lua::ScriptState::Error);
	EXPECT_NE(coroutine.ErrorMessage().find("non-negative"), std::string::npos);
}

TEST_F(LuaCoroutineTest, SequentialScriptsWork)
{
	engine.LoadScript("dosbox.wait_frames(1)", "script-1");
	coroutine.Start();
	coroutine.DispatchFrame(1);
	coroutine.DispatchFrame(2);
	EXPECT_EQ(coroutine.State(), Lua::ScriptState::Completed);

	coroutine.Stop();
	engine.Reset();

	engine.LoadScript("return 42", "script-2");
	Lua::LuaCoroutine coroutine2(engine);
	coroutine2.Start();
	auto state = coroutine2.DispatchFrame(1);
	EXPECT_EQ(state, Lua::ScriptState::Completed);
}

// -- Off-frame wall-deadline reaper (aug-5wu4) --

TEST_F(LuaCoroutineTest, ReapNoOpWhenNotYielded)
{
	// Nothing is yielded, so the reaper must do nothing and not crash.
	EXPECT_EQ(coroutine.ReapStalledWaits(), Lua::ScriptState::Idle);
}

TEST_F(LuaCoroutineTest, ReapTimesOutWaitForTextWhenFramesStall)
{
	// A large frame timeout means only the wall deadline can end this wait.
	// After the script yields we stop dispatching frames (a render stall);
	// the frame path can no longer fire, so the off-frame reaper must.
	coroutine.SetWallCeiling(std::chrono::milliseconds(5));
	engine.LoadScript("dosbox.wait_for_text('NEVERAPPEARS', 1000000)\n",
	                  "reap-wft");
	coroutine.Start();

	EXPECT_EQ(coroutine.DispatchFrame(1), Lua::ScriptState::Yielded);

	std::this_thread::sleep_for(std::chrono::milliseconds(30));

	// No further DispatchFrame: frames have stalled. The reaper resumes the
	// script with a false result, which then runs to completion.
	EXPECT_EQ(coroutine.ReapStalledWaits(), Lua::ScriptState::Completed);
}

TEST_F(LuaCoroutineTest, ReapTimesOutTypeWhenFramesStall)
{
	coroutine.SetWallCeiling(std::chrono::milliseconds(5));
	engine.LoadScript("dosbox.type('hi')\n", "reap-type");
	coroutine.Start();

	EXPECT_EQ(coroutine.DispatchFrame(1), Lua::ScriptState::Yielded);

	std::this_thread::sleep_for(std::chrono::milliseconds(30));

	EXPECT_EQ(coroutine.ReapStalledWaits(), Lua::ScriptState::Completed);
}

TEST_F(LuaCoroutineTest, ReapLeavesWaitForTextAloneBeforeDeadline)
{
	// With frames stalled but the wall deadline far off, the reaper must
	// not prematurely time out a legitimately waiting script.
	coroutine.SetWallCeiling(std::chrono::seconds(30));
	engine.LoadScript("dosbox.wait_for_text('NEVERAPPEARS', 1000000)\n",
	                  "reap-early");
	coroutine.Start();

	EXPECT_EQ(coroutine.DispatchFrame(1), Lua::ScriptState::Yielded);
	EXPECT_EQ(coroutine.ReapStalledWaits(), Lua::ScriptState::Yielded);
}

TEST_F(LuaCoroutineTest, ReapDoesNotTouchWaitFrames)
{
	// wait_frames has no wall ceiling, so even a tiny ceiling plus a sleep
	// must leave it yielded; that stall is the frame path's concern.
	coroutine.SetWallCeiling(std::chrono::milliseconds(1));
	engine.LoadScript("dosbox.wait_frames(100)\n", "reap-frames");
	coroutine.Start();

	EXPECT_EQ(coroutine.DispatchFrame(1), Lua::ScriptState::Yielded);

	std::this_thread::sleep_for(std::chrono::milliseconds(20));

	EXPECT_EQ(coroutine.ReapStalledWaits(), Lua::ScriptState::Yielded);
}

TEST_F(LuaCoroutineTest, ScriptStateNames)
{
	EXPECT_STREQ(Lua::ScriptStateName(Lua::ScriptState::Idle), "idle");
	EXPECT_STREQ(Lua::ScriptStateName(Lua::ScriptState::Running), "running");
	EXPECT_STREQ(Lua::ScriptStateName(Lua::ScriptState::Yielded), "yielded");
	EXPECT_STREQ(Lua::ScriptStateName(Lua::ScriptState::Completed), "completed");
	EXPECT_STREQ(Lua::ScriptStateName(Lua::ScriptState::Error), "error");
}

} // namespace
