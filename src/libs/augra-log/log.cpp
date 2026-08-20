// This file is part of the augra-log Project.
// License: GPL-3.0-or-later. Contact: augra-project@trinity2k.net
//

#include <augra/log.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>

namespace augra {

namespace {

std::string format_timestamp()
{
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    struct tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &time_t_now);
#else
    localtime_r(&time_t_now, &tm_buf);
#endif

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                  static_cast<int>(ms.count()));
    return buf;
}

} // anonymous namespace

const char* log_level_name(LogLevel level)
{
    switch (level) {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO";
        case LogLevel::Warn:  return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "?";
}

// -- LogHandler --

std::string LogHandler::format_output(LogLevel level, const char* component,
                                       const std::string& message) const
{
    std::string result;
    result.reserve(format_.size() + message.size() + 32);

    size_t pos = 0;
    while (pos < format_.size()) {
        size_t brace = format_.find('{', pos);
        if (brace == std::string::npos) {
            result.append(format_, pos);
            break;
        }

        result.append(format_, pos, brace - pos);

        size_t end = format_.find('}', brace);
        if (end == std::string::npos) {
            result.append(format_, brace);
            break;
        }

        auto token = format_.substr(brace + 1, end - brace - 1);
        if (token == "timestamp") {
            result.append(format_timestamp());
        } else if (token == "level") {
            result.append(log_level_name(level));
        } else if (token == "component") {
            result.append(component);
        } else if (token == "message") {
            result.append(message);
        } else {
            result.push_back('{');
            result.append(token);
            result.push_back('}');
        }

        pos = end + 1;
    }

    return result;
}

// -- StderrHandler --

void StderrHandler::emit(LogLevel level, const char* component,
                          const std::string& message)
{
    if (!accepts(level)) return;
    auto line = format_output(level, component, message);
    std::fprintf(stderr, "%s\n", line.c_str());
}

// -- StdoutHandler --

void StdoutHandler::emit(LogLevel level, const char* component,
                          const std::string& message)
{
    if (!accepts(level)) return;
    auto line = format_output(level, component, message);
    std::fprintf(stdout, "%s\n", line.c_str());
    std::fflush(stdout);
}

// -- FileHandler --

FileHandler::FileHandler(const std::string& path, bool append)
    : file_(std::fopen(path.c_str(), append ? "a" : "w"))
{
}

FileHandler::~FileHandler()
{
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

void FileHandler::emit(LogLevel level, const char* component,
                        const std::string& message)
{
    if (!accepts(level) || !file_) return;
    std::lock_guard<std::mutex> lock(file_mutex_);
    auto line = format_output(level, component, message);
    std::fprintf(file_, "%s\n", line.c_str());
    std::fflush(file_);
}

// -- Logger --

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

Logger::Logger()
{
    auto handler = std::make_shared<StderrHandler>();
    handlers_.push_back(handler);
}

void Logger::set_level(LogLevel level)
{
    std::lock_guard<std::mutex> lock(logger_mutex_);
    level_ = level;
}

LogLevel Logger::level() const
{
    std::lock_guard<std::mutex> lock(logger_mutex_);
    return level_;
}

void Logger::set_component_level(const std::string& component, LogLevel level)
{
    std::lock_guard<std::mutex> lock(logger_mutex_);
    component_levels_[component] = level;
}

void Logger::clear_component_level(const std::string& component)
{
    std::lock_guard<std::mutex> lock(logger_mutex_);
    component_levels_.erase(component);
}

void Logger::clear_all_component_levels()
{
    std::lock_guard<std::mutex> lock(logger_mutex_);
    component_levels_.clear();
}

LogLevel Logger::effective_level(const char* component) const
{
    std::lock_guard<std::mutex> lock(logger_mutex_);
    return effective_level_unlocked(component);
}

LogLevel Logger::effective_level_unlocked(const char* component) const
{
    if (!component)
        return level_;
    auto it = component_levels_.find(component);
    if (it != component_levels_.end())
        return it->second;
    return level_;
}

void Logger::add_handler(std::shared_ptr<LogHandler> handler)
{
    std::lock_guard<std::mutex> lock(logger_mutex_);
    handlers_.push_back(std::move(handler));
}

void Logger::remove_handler(const std::shared_ptr<LogHandler>& handler)
{
    std::lock_guard<std::mutex> lock(logger_mutex_);
    handlers_.erase(
        std::remove(handlers_.begin(), handlers_.end(), handler),
        handlers_.end());
}

void Logger::clear_handlers()
{
    std::lock_guard<std::mutex> lock(logger_mutex_);
    handlers_.clear();
}

void Logger::add_sink(LogSink sink)
{
    std::lock_guard<std::mutex> lock(logger_mutex_);
    sinks_.push_back(std::move(sink));
}

void Logger::clear_sinks()
{
    std::lock_guard<std::mutex> lock(logger_mutex_);
    sinks_.clear();
}

void Logger::log(LogLevel level, const char* component, const std::string& msg)
{
    if (!component)
        component = "";
    std::lock_guard<std::mutex> lock(logger_mutex_);
    if (level < effective_level_unlocked(component))
        return;

    for (auto& handler : handlers_)
        handler->emit(level, component, msg);

    for (auto& sink : sinks_)
        sink(level, component, msg);
}

std::string Logger::format_msg(const char* fmt)
{
    return fmt;
}

} // namespace augra
