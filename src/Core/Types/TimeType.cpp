#include "TimeType.h"

#include <ctype.h>
#include <stdio.h>

namespace
{
bool parsePart(const String &text, uint8_t maximum, uint8_t &out)
{
    if (text.length() == 0 || text.length() > 2)
        return false;
    unsigned value = 0;
    for (size_t i = 0; i < text.length(); ++i)
    {
        if (!isdigit(static_cast<unsigned char>(text[i])))
            return false;
        value = value * 10U + static_cast<unsigned>(text[i] - '0');
    }
    if (value > maximum)
        return false;
    out = static_cast<uint8_t>(value);
    return true;
}
}

TimeType::TimeType(uint8_t hour, uint8_t minute, uint8_t second)
{
    if (hour < 24 && minute < 60 && second < 60)
    {
        hours_ = hour;
        minutes_ = minute;
        seconds_ = second;
    }
}

String TimeType::toString() const
{
    char encoded[9];
    snprintf(encoded, sizeof(encoded), "%02u:%02u:%02u",
             static_cast<unsigned>(hours_), static_cast<unsigned>(minutes_),
             static_cast<unsigned>(seconds_));
    return String(encoded);
}

bool TimeType::operator==(const TimeType &other) const
{
    return hours_ == other.hours_ && minutes_ == other.minutes_ &&
           seconds_ == other.seconds_;
}

String encodeTime(const TimeType &time)
{
    return time.toString();
}

bool decodeTime(const String &encoded, TimeType &time)
{
    String text = encoded;
    text.trim();
    if (text.length() == 0)
        return false;

    uint8_t hour = 0;
    uint8_t minute = 0;
    uint8_t second = 0;
    const int firstColon = text.indexOf(':');
    if (firstColon >= 0)
    {
        const int secondColon = text.indexOf(':', firstColon + 1);
        if (text.indexOf(':', secondColon >= 0 ? secondColon + 1 : firstColon + 1) >= 0)
            return false;
        if (!parsePart(text.substring(0, firstColon), 23, hour))
            return false;
        if (secondColon < 0)
        {
            if (!parsePart(text.substring(firstColon + 1), 59, minute))
                return false;
        }
        else
        {
            if (!parsePart(text.substring(firstColon + 1, secondColon), 59, minute) ||
                !parsePart(text.substring(secondColon + 1), 59, second))
                return false;
        }
    }
    else
    {
        if (text.length() != 4 && text.length() != 6)
            return false;
        if (!parsePart(text.substring(0, 2), 23, hour) ||
            !parsePart(text.substring(2, 4), 59, minute) ||
            (text.length() == 6 && !parsePart(text.substring(4, 6), 59, second)))
            return false;
    }

    time = TimeType(hour, minute, second);
    return true;
}

bool TimeType::matches(const time_t &epoch, bool include_seconds) const
{
    struct tm tm;
    localtime_r(&epoch, &tm);
    if (tm.tm_hour != hours_ || tm.tm_min != minutes_)
        return false;
    if (include_seconds && tm.tm_sec != seconds_)
        return false;
    return true;
}