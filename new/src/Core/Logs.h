#pragma once

#include <Arduino.h>

// NM_LOG_LEVEL can be supplied by the project's configuration.
// 0 disables logging; 1-5 enable Error, Warning, Info, Debug, and Trace.
#define NM_LOG_LEVEL_OFF 0
#define NM_LOG_LEVEL_ERROR 1
#define NM_LOG_LEVEL_WARNING 2
#define NM_LOG_LEVEL_INFO 3
#define NM_LOG_LEVEL_DEBUG 4
#define NM_LOG_LEVEL_TRACE 5

#ifndef NM_LOG_LEVEL
#define NM_LOG_LEVEL NM_LOG_LEVEL_OFF
#endif

namespace NMLog
{
enum class Level : uint8_t
{
    Error = NM_LOG_LEVEL_ERROR,
    Warning = NM_LOG_LEVEL_WARNING,
    Info = NM_LOG_LEVEL_INFO,
    Debug = NM_LOG_LEVEL_DEBUG,
    Trace = NM_LOG_LEVEL_TRACE
};

void write(Level level, const char *module, const char *format, ...);
const char *status(bool success);
}

#if NM_LOG_LEVEL >= NM_LOG_LEVEL_ERROR
#define LOG_ERROR(module, ...) ::NMLog::write(::NMLog::Level::Error, module, __VA_ARGS__)
#else
#define LOG_ERROR(module, ...) ((void)0)
#endif

#if NM_LOG_LEVEL >= NM_LOG_LEVEL_WARNING
#define LOG_WARNING(module, ...) ::NMLog::write(::NMLog::Level::Warning, module, __VA_ARGS__)
#else
#define LOG_WARNING(module, ...) ((void)0)
#endif

#if NM_LOG_LEVEL >= NM_LOG_LEVEL_INFO
#define LOG(module, ...) ::NMLog::write(::NMLog::Level::Info, module, __VA_ARGS__)
#else
#define LOG(module, ...) ((void)0)
#endif

#if NM_LOG_LEVEL >= NM_LOG_LEVEL_DEBUG
#define LOG_DEBUG(module, ...) ::NMLog::write(::NMLog::Level::Debug, module, __VA_ARGS__)
#else
#define LOG_DEBUG(module, ...) ((void)0)
#endif

#if NM_LOG_LEVEL >= NM_LOG_LEVEL_TRACE
#define LOG_TRACE(module, ...) ::NMLog::write(::NMLog::Level::Trace, module, __VA_ARGS__)
#else
#define LOG_TRACE(module, ...) ((void)0)
#endif

#define OK_LOG(value) (::NMLog::status(static_cast<bool>(value)))
