// SPDX-FileCopyrightText:  2002-2021 The DOSBox Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_LOGGING_H
#define DOSBOX_LOGGING_H

#include <cstdio>
#include <string>

#include "misc/compiler.h"

#include "loguru/loguru.hpp"

enum LOG_TYPES {
	LOG_ALL,
	LOG_VGA, LOG_VGAGFX,LOG_VGAMISC,LOG_INT10,
	LOG_SB,LOG_DMACONTROL,
	LOG_FPU,LOG_CPU,LOG_PAGING,
	LOG_FCB,LOG_FILES,LOG_IOCTL,LOG_EXEC,LOG_DOSMISC,
	LOG_PIT,LOG_KEYBOARD,LOG_PIC,
	LOG_MOUSE,LOG_BIOS,LOG_GUI,LOG_MISC,
	LOG_IO,
	LOG_PCI,
	LOG_REELMAGIC,
	LOG_MAX
};

enum LOG_SEVERITIES {
	LOG_NORMAL,
	LOG_WARN,
	LOG_ERROR
};

#if C_DEBUGGER
class LOG 
{ 
	LOG_TYPES       d_type;
	LOG_SEVERITIES  d_severity;
public:

	LOG (LOG_TYPES type , LOG_SEVERITIES severity):
		d_type(type),
		d_severity(severity)
		{}
	        void operator()(const char* buf, ...)
	                GCC_ATTRIBUTE(__format__(__printf__, 2, 3)); //../src/debug/debug_gui.cpp
};

void DEBUG_ShowMsg(const char* format, ...)
        GCC_ATTRIBUTE(__format__(__printf__, 1, 2));
#define LOG_MSG DEBUG_ShowMsg

#define LOG_INFO(...)    LOG(LOG_ALL, LOG_NORMAL)(__VA_ARGS__)
#define LOG_WARNING(...) LOG(LOG_ALL, LOG_WARN)(__VA_ARGS__)
#define LOG_ERR(...)     LOG(LOG_ALL, LOG_ERROR)(__VA_ARGS__)

#else // C_DEBUGGER

struct LOG
{
	inline LOG(LOG_TYPES, LOG_SEVERITIES){ }
	inline void operator()(const char*, ...) const {}
}; //add missing operators to here

	//try to avoid anything smaller than bit32...
void GFX_ShowMsg(const char* format, ...)
        GCC_ATTRIBUTE(__format__(__printf__, 1, 2));

#include "augra/log.h"
#include <cstdarg>

// C-style varargs shim so that packed struct fields (which cannot
// bind to C++ template references) pass through LOG_MSG unchanged.
namespace augra_compat {

GCC_ATTRIBUTE(__format__(__printf__, 2, 3))
inline void log_info(const char* component, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[2048];
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    augra::Logger::instance().log(augra::LogLevel::Info, component, buf);
}

GCC_ATTRIBUTE(__format__(__printf__, 2, 3))
inline void log_warn(const char* component, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[2048];
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    augra::Logger::instance().log(augra::LogLevel::Warn, component, buf);
}

GCC_ATTRIBUTE(__format__(__printf__, 2, 3))
inline void log_error(const char* component, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[2048];
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    augra::Logger::instance().log(augra::LogLevel::Error, component, buf);
}

GCC_ATTRIBUTE(__format__(__printf__, 2, 3))
inline void log_debug(const char* component, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[2048];
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    augra::Logger::instance().log(augra::LogLevel::Debug, component, buf);
}

GCC_ATTRIBUTE(__format__(__printf__, 2, 3))
inline void log_trace(const char* component, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buf[2048];
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    augra::Logger::instance().log(augra::LogLevel::Trace, component, buf);
}

} // namespace augra_compat

#define LOG_MSG(...)	augra_compat::log_info("dosbox", __VA_ARGS__)

#define LOG_INFO(...)		augra_compat::log_info("dosbox", __VA_ARGS__)
#define LOG_WARNING(...)	augra_compat::log_warn("dosbox", __VA_ARGS__)
#define LOG_ERR(...)		augra_compat::log_error("dosbox", __VA_ARGS__)

#endif // C_DEBUGGER

#ifdef NDEBUG
#define LOG_DEBUG(...)
#define LOG_TRACE(...)
#else
#define LOG_DEBUG(...)	augra_compat::log_debug("dosbox", __VA_ARGS__)
#define LOG_TRACE(...)	augra_compat::log_trace("dosbox", __VA_ARGS__)
#endif // NDEBUG

#endif
