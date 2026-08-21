// This file is part of the augra-log Project.
// License: GPL-3.0-or-later. Contact: augra-project@trinity2k.net
//

#ifndef AUGRA_LOG_H
#define AUGRA_LOG_H

#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace augra {

enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
};

const char* log_level_name(LogLevel level);

using LogSink = std::function<void(LogLevel level,
                                   const char* component,
                                   const std::string& message)>;

// -- Handlers --

class LogHandler {
public:
    virtual ~LogHandler() = default;

    void set_level(LogLevel level) { level_ = level; }
    LogLevel level() const { return level_; }

    void set_format(const std::string& fmt) { format_ = fmt; }
    const std::string& format_string() const { return format_; }

    bool accepts(LogLevel level) const { return level >= level_; }

    virtual void emit(LogLevel level, const char* component,
                      const std::string& message) = 0;

protected:
    std::string format_output(LogLevel level, const char* component,
                              const std::string& message) const;

private:
    LogLevel level_ = LogLevel::Trace;
    std::string format_ = "{timestamp} [{level}] {component}: {message}";
};

class StderrHandler : public LogHandler {
public:
    void emit(LogLevel level, const char* component,
              const std::string& message) override;
};

class StdoutHandler : public LogHandler {
public:
    void emit(LogLevel level, const char* component,
              const std::string& message) override;
};

class FileHandler : public LogHandler {
public:
    explicit FileHandler(const std::string& path, bool append = true);
    ~FileHandler() override;

    FileHandler(const FileHandler&) = delete;
    FileHandler& operator=(const FileHandler&) = delete;

    bool is_open() const { return file_ != nullptr; }

    void emit(LogLevel level, const char* component,
              const std::string& message) override;

private:
    std::FILE* file_ = nullptr;
    std::mutex file_mutex_;
};

// -- Logger --

class Logger {
public:
    static Logger& instance();

    void set_level(LogLevel level);
    LogLevel level() const;

    void set_component_level(const std::string& component, LogLevel level);
    void clear_component_level(const std::string& component);
    void clear_all_component_levels();
    LogLevel effective_level(const char* component) const;

    void add_handler(std::shared_ptr<LogHandler> handler);
    void remove_handler(const std::shared_ptr<LogHandler>& handler);
    void clear_handlers();

    void add_sink(LogSink sink);
    void clear_sinks();

    void log(LogLevel level, const char* component, const std::string& msg);

    template<typename... Args>
    void trace(const char* component, const char* fmt, Args&&... args);

    template<typename... Args>
    void debug(const char* component, const char* fmt, Args&&... args);

    template<typename... Args>
    void info(const char* component, const char* fmt, Args&&... args);

    template<typename... Args>
    void warn(const char* component, const char* fmt, Args&&... args);

    template<typename... Args>
    void error(const char* component, const char* fmt, Args&&... args);

private:
    Logger();

    static std::string format_msg(const char* fmt);

    template<typename... Args>
    static std::string format_msg(const char* fmt, Args&&... args);

    LogLevel effective_level_unlocked(const char* component) const;

    mutable std::mutex logger_mutex_;
    LogLevel level_ = LogLevel::Info;
    std::unordered_map<std::string, LogLevel> component_levels_;
    std::vector<std::shared_ptr<LogHandler>> handlers_;
    std::vector<LogSink> sinks_;
};

// -- Template implementations --

template<typename... Args>
void Logger::trace(const char* component, const char* fmt, Args&&... args)
{
    if (effective_level(component) <= LogLevel::Trace)
        log(LogLevel::Trace, component, format_msg(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
void Logger::debug(const char* component, const char* fmt, Args&&... args)
{
    if (effective_level(component) <= LogLevel::Debug)
        log(LogLevel::Debug, component, format_msg(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
void Logger::info(const char* component, const char* fmt, Args&&... args)
{
    if (effective_level(component) <= LogLevel::Info)
        log(LogLevel::Info, component, format_msg(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
void Logger::warn(const char* component, const char* fmt, Args&&... args)
{
    if (effective_level(component) <= LogLevel::Warn)
        log(LogLevel::Warn, component, format_msg(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
void Logger::error(const char* component, const char* fmt, Args&&... args)
{
    if (effective_level(component) <= LogLevel::Error)
        log(LogLevel::Error, component, format_msg(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
std::string Logger::format_msg(const char* fmt, Args&&... args)
{
    int size = std::snprintf(nullptr, 0, fmt, args...);
    if (size <= 0)
        return fmt;
    std::string result(static_cast<size_t>(size), '\0');
    std::snprintf(result.data(), result.size() + 1, fmt, args...);
    return result;
}

// Free-function shortcuts
template<typename... Args>
void log_trace(const char* component, const char* fmt, Args&&... args)
{
    Logger::instance().trace(component, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void log_debug(const char* component, const char* fmt, Args&&... args)
{
    Logger::instance().debug(component, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void log_info(const char* component, const char* fmt, Args&&... args)
{
    Logger::instance().info(component, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void log_warn(const char* component, const char* fmt, Args&&... args)
{
    Logger::instance().warn(component, fmt, std::forward<Args>(args)...);
}

template<typename... Args>
void log_error(const char* component, const char* fmt, Args&&... args)
{
    Logger::instance().error(component, fmt, std::forward<Args>(args)...);
}

} // namespace augra

#endif // AUGRA_LOG_H
