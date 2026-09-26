#pragma once

#include <Arduino.h>
#include <time.h>

namespace NightMare
{
namespace Time
{
constexpr time_t SecondsPerMinute = 60;
constexpr time_t SecondsPerHour = 60 * SecondsPerMinute;

enum TimeStampFormat
{
    DateAndTime,
    OnlyDate,
    SmallDate,
    OnlyTime,
    OnlyTimeWithSeconds,
    OnlyTimeLive,
    DowDate,
    TimeSinceStamp,
    CountdownFromTimestamp
};

// Unix seconds from the ESP system clock. Calendar component accessors are UTC.
time_t now();
bool valid();
bool setEpoch(time_t epoch);
int second(time_t epoch = now());
int minute(time_t epoch = now());
int hour(time_t epoch = now());
int day(time_t epoch = now());
int month(time_t epoch = now());
int year(time_t epoch = now());

// Human-facing formatting uses the process TZ configured by the application.
String timestampToDateString(time_t timestamp, TimeStampFormat format = DateAndTime);

inline String timeString(time_t timestamp = now())
{
    return timestampToDateString(timestamp, OnlyTime);
}

inline String fullTimeString(time_t timestamp = now())
{
    return timestampToDateString(timestamp, OnlyTimeWithSeconds);
}

inline String dateString(time_t timestamp = now())
{
    return timestampToDateString(timestamp, OnlyDate);
}

// Returns the next local occurrence of HH:MM, or 0 for malformed input.
time_t timestampOfNextOccurrence(const String &timeString);
}
}

// Compatibility helpers retained from the legacy Misc time API.
#define TIME(...) NightMare::Time::timeString(__VA_ARGS__)
#define TIME_STR(...) TIME(__VA_ARGS__).c_str()
#define TIME_FULL(...) NightMare::Time::fullTimeString(__VA_ARGS__)
#define TIME_FULL_STR(...) TIME_FULL(__VA_ARGS__).c_str()
#define DATE(...) NightMare::Time::dateString(__VA_ARGS__)
#define DATE_STR(...) DATE(__VA_ARGS__).c_str()
#define TIME_SINCE(var) NightMare::Time::timestampToDateString((var), NightMare::Time::TimeSinceStamp)
#define TIME_SINCE_STR(var) TIME_SINCE(var).c_str()
#define COUNTDOWN(var) NightMare::Time::timestampToDateString((var), NightMare::Time::CountdownFromTimestamp)
#define COUNTDOWN_STR(var) COUNTDOWN(var).c_str()
#define LIVE_TIME(var) NightMare::Time::timestampToDateString((var), NightMare::Time::OnlyTimeLive)
#define LIVE_TIME_STR(var) LIVE_TIME(var).c_str()
#define DOW_DATE(var) NightMare::Time::timestampToDateString((var), NightMare::Time::DowDate)
#define DOW_DATE_STR(var) DOW_DATE(var).c_str()
#define DATE_NO_YEAR(var) NightMare::Time::timestampToDateString((var), NightMare::Time::SmallDate)
#define DATE_NO_YEAR_STR(var) DATE_NO_YEAR(var).c_str()


// Usefull Definitions
#define HOUR  60 * 60
#define MINUTE 60
#define SECOND 1
#define DAY 24 * HOUR
#define IN_MS 1000
#define MINUTE_MS 60 * MS
#define HOUR_MS 60 * MINUTE_MS
#define SECOND_MS 1000