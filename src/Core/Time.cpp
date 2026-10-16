#include "Time.h"

#include <sys/time.h>

namespace NightMare
{
namespace Time
{
namespace
{
constexpr time_t EarliestValidEpoch = 1577836800; // 2020-01-01 UTC

tm utcParts(time_t epoch)
{
    tm result{};
    gmtime_r(&epoch, &result);
    return result;
}

tm localParts(time_t epoch)
{
    tm result{};
    localtime_r(&epoch, &result);
    return result;
}

std::string strftimeString(const tm &parts, const char *format)
{
    char buffer[32] = {};
    return strftime(buffer, sizeof(buffer), format, &parts) != 0 ? std::string(buffer) : std::string();
}

std::string durationString(uint64_t seconds, bool countdown, bool negative)
{
    std::string value;
    if (negative)
        value += '-';

    if (!countdown)
    {
        if (seconds < 90)
            return value + std::to_string(static_cast<unsigned long>(seconds)) + " sec.";
        if (seconds < 90 * SecondsPerMinute)
            return value + std::to_string(static_cast<unsigned long>(seconds / SecondsPerMinute)) + " min.";
        return value + std::to_string(static_cast<unsigned long>(seconds / SecondsPerHour)) + " hours.";
    }

    const uint64_t hours = seconds / SecondsPerHour;
    const uint8_t minutes = static_cast<uint8_t>((seconds / SecondsPerMinute) % 60);
    const uint8_t remainingSeconds = static_cast<uint8_t>(seconds % 60);
    char buffer[32] = {};
    if (hours != 0)
        snprintf(buffer, sizeof(buffer), "%s%02llu:%02u", negative ? "-" : "",
                 static_cast<unsigned long long>(hours), minutes);
    else
        snprintf(buffer, sizeof(buffer), "%s%02u:%02u", negative ? "-" : "",
                 minutes, remainingSeconds);
    return std::string(buffer);
}
}

time_t now()
{
    return ::time(nullptr);
}

bool valid()
{
    return now() >= EarliestValidEpoch;
}

bool setEpoch(time_t epoch)
{
    if (epoch < EarliestValidEpoch)
        return false;
    const timeval value{epoch, 0};
    return settimeofday(&value, nullptr) == 0;
}

int second(time_t epoch) { return utcParts(epoch).tm_sec; }
int minute(time_t epoch) { return utcParts(epoch).tm_min; }
int hour(time_t epoch) { return utcParts(epoch).tm_hour; }
int day(time_t epoch) { return utcParts(epoch).tm_mday; }
int month(time_t epoch) { return utcParts(epoch).tm_mon + 1; }
int year(time_t epoch) { return utcParts(epoch).tm_year + 1900; }

std::string timestampToDateString(time_t timestamp, TimeStampFormat format)
{
    const time_t current = now();
    if (format == TimeSinceStamp || format == CountdownFromTimestamp)
    {
        const bool timestampIsPast = timestamp < current;
        const uint64_t difference = static_cast<uint64_t>(timestampIsPast ? current - timestamp
                                                                          : timestamp - current);
        const bool negative = format == CountdownFromTimestamp ? timestampIsPast
                                                                : !timestampIsPast;
        return durationString(difference, format == CountdownFromTimestamp, negative);
    }

    const tm parts = localParts(timestamp);
    switch (format)
    {
    case DateAndTime:
        return strftimeString(parts, "%H:%M %d/%m/%y");
    case OnlyDate:
        return strftimeString(parts, "%d/%m/%y");
    case SmallDate:
        return strftimeString(parts, "%d/%m");
    case OnlyTime:
        return strftimeString(parts, "%H:%M");
    case OnlyTimeWithSeconds:
        return strftimeString(parts, "%H:%M:%S");
    case OnlyTimeLive:
    {
        std::string value = strftimeString(parts, "%H:%M");
        if ((current & 1) != 0 && value.length() >= 3)
            value[2] = ' ';
        return value;
    }
    case DowDate:
        return strftimeString(parts, "%a %d/%m");
    case TimeSinceStamp:
    case CountdownFromTimestamp:
        break;
    }
    return std::string();
}

time_t timestampOfNextOccurrence(const std::string &timeString)
{
    if (!valid())
        return 0;
    if (timeString.length() != 5 || timeString[2] != ':' ||
        !std::isdigit(static_cast<unsigned char>(timeString[0])) ||
        !std::isdigit(static_cast<unsigned char>(timeString[1])) ||
        !std::isdigit(static_cast<unsigned char>(timeString[3])) ||
        !std::isdigit(static_cast<unsigned char>(timeString[4])))
        return 0;

    const int targetHour = NightMare::Text::toLong(NightMare::Text::slice(timeString, 0, 2));
    const int targetMinute = NightMare::Text::toLong(NightMare::Text::slice(timeString, 3, 5));
    if (targetHour > 23 || targetMinute > 59)
        return 0;

    const time_t current = now();
    tm target = localParts(current);
    target.tm_hour = targetHour;
    target.tm_min = targetMinute;
    target.tm_sec = 0;
    target.tm_isdst = -1;
    time_t result = mktime(&target);
    if (result <= current)
    {
        target.tm_mday += 1;
        target.tm_isdst = -1;
        result = mktime(&target);
    }
    return result;
}
}
}
