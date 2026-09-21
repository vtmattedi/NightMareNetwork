#include "Logs.h"

#include <cstdarg>
#include <cstdio>

namespace NMLog
{
    const char *status(bool success)
    {
#if (NM_LOG_USE_ANSI)
        return success ? "[\033[1;32mOK\033[0m]" : "[\033[1;31mERROR\033[0m]";
#else
        return success ? "[OK]" : "[ERROR]";
#endif
    }

    void write(Level level, const char *module, const char *format, ...)
    {
#if NM_LOG_LEVEL > NM_LOG_LEVEL_OFF
        if (format == nullptr || static_cast<uint8_t>(level) > NM_LOG_LEVEL)
            return;
#if (NM_LOG_USE_ANSI)
        const char *levelName = "INFO";
        switch (level)
        {
        case Level::Error:
            levelName = "\033[1;31mERROR\033[0m";
            break;
        case Level::Warning:
            levelName = "\033[1;33mWARNING\033[0m";
            break;
        case Level::Info:
            levelName = "\033[1;32mINFO\033[0m";
            break;
        case Level::Debug:
            levelName = "\033[1;34mDEBUG\033[0m";
            break;
        case Level::Trace:
            levelName = "\033[1;35mTRACE\033[0m";
            break;
        }
#else
        const char *levelName = "INFO";
        switch (level)
        {
        case Level::Error:
            levelName = "ERROR";
            break;
        case Level::Warning:
            levelName = "WARNING";
            break;
        case Level::Info:
            levelName = "INFO";
            break;
        case Level::Debug:
            levelName = "DEBUG";
            break;
        case Level::Trace:
            levelName = "TRACE";
            break;
        }
#endif
        char message[256];
        va_list args;
        va_start(args, format);
        std::vsnprintf(message, sizeof(message), format, args);
        va_end(args);
#if (NM_LOG_USE_ANSI)
#define NM_LOG_TIME_FORMAT "[\033[90m%lu\033[0m] [%s] [%s] %s\n"
#else
#define NM_LOG_TIME_FORMAT "[%lu] [%s] [%s] %s\n"
#endif
        Serial.printf(NM_LOG_TIME_FORMAT, static_cast<unsigned long>(millis()),
                      levelName, module != nullptr ? module : "Core", message);
#else
        (void)level;
        (void)module;
        (void)format;
#endif
    }
}
