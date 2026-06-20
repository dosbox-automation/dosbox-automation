// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#ifndef DOSBOX_LUA_DEBUG_LOG_H
#define DOSBOX_LUA_DEBUG_LOG_H

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

namespace Lua {

class DebugLog {
public:
	DebugLog() = default;
	~DebugLog();

	DebugLog(const DebugLog&)            = delete;
	DebugLog& operator=(const DebugLog&) = delete;

	bool Open(const std::string& directory, const std::string& script_name);
	void Close();
	bool IsOpen() const
	{
		return file != nullptr;
	}

	void Trace(uint64_t frame, const char* fmt, ...);
	void TraceMessage(uint64_t frame, const std::string& msg);

	const std::string& FilePath() const
	{
		return file_path;
	}

private:
	FILE* file            = nullptr;
	std::string file_path = {};

	using Clock             = std::chrono::steady_clock;
	Clock::time_point start = {};

	std::string FormatTimestamp() const;
};

} // namespace Lua

#endif
