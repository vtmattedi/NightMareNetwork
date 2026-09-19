#include "Logs.h"

#include <cstdarg>
#include <cstdio>

namespace NMLog
{
const char *status(bool success)
{
    return success ? "[OK]" : "[ERROR]";
}

void write(Level level, const char *module, const char *format, ...)
{
#if NM_LOG_LEVEL > NM_LOG_LEVEL_OFF
    if (format == nullptr || static_cast<uint8_t>(level) > NM_LOG_LEVEL)
        return;

    const char *levelName = "INFO";
    switch (level)
    {
    case Level::Error:   levelName = "ERROR"; break;
    case Level::Warning: levelName = "WARNING"; break;
    case Level::Info:    levelName = "INFO"; break;
    case Level::Debug:   levelName = "DEBUG"; break;
    case Level::Trace:   levelName = "TRACE"; break;
    }

    char message[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);

    Serial.printf("[%lu] [%s] [%s] %s\n", static_cast<unsigned long>(millis()),
                  levelName, module != nullptr ? module : "Core", message);
#else
    (void)level;
    (void)module;
    (void)format;
#endif
}
}
