/*----------------------------------------------------------*/
///
///@file Misc.h -
/// Implements miscellaneous functions NightMare Network.
/// Author: Vitor Mattedi Carvalho
/// Date: 21-02-2024
/// Version: 1.1
///         Structure of the lib changed.
/*----------------------------------------------------------*/

#pragma once
#include <Modules.config.h>
#ifdef COMPILE_MISC
#include <Arduino.h>
#include <TimeLib.h>
#include <Core/LVGL_Util.h>

#define HOUR 3600
#define MINUTE 60
// Returns "HH:MM"
#define TIME(var) timestampToDateString(var,TimeStampFormat::OnlyTime)
#define TIME_STR(var) TIME(var).c_str()
// Returns "HH:MM:SS"
#define TIME_FULL(var) timestampToDateString(var,TimeStampFormat::OnlyTimeWithSeconds)
#define TIME_FULL_STR(var) TIME_FULL(var).c_str()
// Returns "DD-MM-YYYY"
#define DATE(var) timestampToDateString(var,TimeStampFormat::OnlyDate)
#define DATE_STR(var) DATE(var).c_str()
#define TIME_SINCE(var) timestampToDateString(var,TimeStampFormat::TimeSinceStamp)
#define TIME_SINCE_STR(var) TIME_SINCE(var).c_str()
#define COUNTDOWN(var) timestampToDateString(var,TimeStampFormat::CountdownFromTimestamp)
#define COUNTDOWN_STR(var) COUNTDOWN(var).c_str()
// Returns "HH:MM" with live updating: ':' will blink every second
#define LIVE_TIME(var) timestampToDateString(var,TimeStampFormat::OnlyTimeLive)
#define LIVE_TIME_STR(var) LIVE_TIME(var).c_str()
// Returns "DayOfWeek, DD-MM-YYYY"
#define DOW_DATE(var) timestampToDateString(var,TimeStampFormat::DowDate)
#define DOW_DATE_STR(var) DOW_DATE(var).c_str()
// Returns "DD-MM"
#define DATE_NO_YEAR(var) timestampToDateString(var,TimeStampFormat::SmallDate)
#define DATE_NO_YEAR_STR(var) timestampToDateString(var,TimeStampFormat::SmallDate).c_str()


#define FORMAT_BUFFER_SIZE 1024 // max String length

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

String timestampToDateString(uint32_t timestamp, const TimeStampFormat _format = DateAndTime);
String formatString(const char *format, ...);
float ramUsagePercent();
const char *getBootReason(int reason);
// uint32_t timestampOfNextOccurrence(uint8_t hour, uint8_t minute, uint8_t second = 0);
uint32_t timestampOfNextOccurrence(String timeString);
float fsUsagePercent();
// getSystemStatus() lives in Core/SystemStatus.h: it aggregates WiFi, MQTT, HTTP and the async
// command system, and Misc is a leaf that those modules sit above.
#endif