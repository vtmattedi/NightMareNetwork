#include "Logs.h"

#include <cstdarg>
#include <cstdio>
#if NM_LOG_USE_TIME_OF_DAY
#include <ctime>
#include "Time.h"
#endif

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
        // The stamp is text so both clocks share one format: millis() before the first time sync.
        char stamp[16];
        std::snprintf(stamp, sizeof(stamp), "%lu", static_cast<unsigned long>(millis()));
#if NM_LOG_USE_TIME_OF_DAY
        if (NightMare::Time::valid())
        {
            const time_t now = NightMare::Time::now();
            struct tm local;
            localtime_r(&now, &local); // the process TZ the application configured
            std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);
        }
#endif
#if (NM_LOG_USE_ANSI)
#define NM_LOG_TIME_FORMAT "[\033[90m%s\033[0m] [%s] [%s] %s\n"
#else
#define NM_LOG_TIME_FORMAT "[%s] [%s] [%s] %s\n"
#endif
        Serial.printf(NM_LOG_TIME_FORMAT, stamp,
                      levelName, module != nullptr ? module : "Core", message);
#else
        (void)level;
        (void)module;
        (void)format;
#endif
    }
}
