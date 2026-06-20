// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "lua/lua_debug_log.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>

namespace Lua {

DebugLog::~DebugLog()
{
	Close();
}

bool DebugLog::Open(const std::string& directory, const std::string& script_name)
{
	Close();

	std::filesystem::create_directories(directory);

	const auto now    = std::chrono::system_clock::now();
	const auto time_t = std::chrono::system_clock::to_time_t(now);
	struct tm tm_buf  = {};
#if defined(WIN32)
	localtime_s(&tm_buf, &time_t);
#else
	localtime_r(&time_t, &tm_buf);
#endif

	char timestamp[32] = {};
	std::strftime(timestamp, sizeof(timestamp), "%Y%m%d-%H%M%S", &tm_buf);

	file_path = (std::filesystem::path(directory) /
	             ("lua-debug-" + std::string(timestamp) + "-" + script_name + ".log"))
	                    .string();

	file = std::fopen(file_path.c_str(), "w");
	if (!file) {
		file_path.clear();
		return false;
	}

	start = Clock::now();

	std::fprintf(file, "# lua debug log: %s\n", script_name.c_str());
	std::fprintf(file, "# started: %s\n\n", timestamp);
	std::fflush(file);
	return true;
}

void DebugLog::Close()
{
	if (file) {
		std::fflush(file);
		std::fclose(file);
		file = nullptr;
	}
	file_path.clear();
}

std::string DebugLog::FormatTimestamp() const
{
	const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
	        Clock::now() - start);
	const auto total_ms = elapsed.count();
	const auto h        = total_ms / 3600000;
	const auto m        = (total_ms % 3600000) / 60000;
	const auto s        = (total_ms % 60000) / 1000;
	const auto ms       = total_ms % 1000;

	char buf[32] = {};
	std::snprintf(buf,
	              sizeof(buf),
	              "%02lld:%02lld:%02lld.%03lld",
	              static_cast<long long>(h),
	              static_cast<long long>(m),
	              static_cast<long long>(s),
	              static_cast<long long>(ms));
	return buf;
}

void DebugLog::Trace(const uint64_t frame, const char* fmt, ...)
{
	if (!file) {
		return;
	}

	std::fprintf(file,
	             "[F:%05llu T:%s] ",
	             static_cast<unsigned long long>(frame),
	             FormatTimestamp().c_str());

	va_list args;
	va_start(args, fmt);
	std::vfprintf(file, fmt, args);
	va_end(args);

	std::fprintf(file, "\n");
	std::fflush(file);
}

void DebugLog::TraceMessage(const uint64_t frame, const std::string& msg)
{
	Trace(frame, "debugmsg: %s", msg.c_str());
}

} // namespace Lua
