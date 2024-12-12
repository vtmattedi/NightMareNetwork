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
#include <Arduino.h>
#include <TimeLib.h>

//comment this line if you don't want to compile LVGL assist functions
//or the project does not contain LVGL
#define COMPILE_LVGL_ASSIST true
#ifdef COMPILE_LVGL_ASSIST
#include <lvgl.h>
#endif

#define HOUR 3600
#define MINUTE 60
#define TIME(var) timestampToDateString(var,TimeStampFormat::OnlyTime)
#define TIME_STR(var) TIME(var).c_str()
#define DATE(var) timestampToDateString(var,TimeStampFormat::OnlyDate)
#define DATE_STR(var) DATE(var).c_str()
#define TIME_SINCE(var) timestampToDateString(var,TimeStampFormat::TimeSinceStamp)
#define TIME_SINCE_STR(var) TIME_SINCE(var).c_str()
#define COUNTDOWN(var) timestampToDateString(var,TimeStampFormat::CountdownFromTimestamp)
#define COUNTDOWN_STR(var) COUNTDOWN(var).c_str()
#define LIVE_TIME(var) timestampToDateString(var,TimeStampFormat::OnlyTimeLive)
#define LIVE_TIME_STR(var) LIVE_TIME(var).c_str()
#define DOW_DATE(var) timestampToDateString(var,TimeStampFormat::DowDate)
#define DOW_DATE_STR(var) DOW_DATE(var).c_str()
#define DATE_NO_YEAR_STR(var) timestampToDateString(var,TimeStampFormat::SmallDate).c_str()
#define DATE_NO_YEAR(var) timestampToDateString(var,TimeStampFormat::SmallDate)


#define FORMAT_BUFFER_SIZE 1024 // max String length

enum TimeStampFormat
{
  DateAndTime,
  OnlyDate,
  SmallDate,
  OnlyTime,
  OnlyTimeLive,
  DowDate,
  TimeSinceStamp,
  CountdownFromTimestamp
};

String timestampToDateString(uint32_t timestamp, const TimeStampFormat _format = DateAndTime);
String formatString(const char *format, ...);
const char *mqttStatusToString(int);

#ifdef COMPILE_LVGL_ASSIST
#pragma region "LVGL Helper Functions"
void set_lv_flag(lv_obj_t *obj, bool value, lv_obj_flag_t flag = LV_OBJ_FLAG_HIDDEN);
void set_lv_state(lv_obj_t *obj, bool value, lv_state_t state = LV_STATE_CHECKED);
void set_lv_visible(lv_obj_t *obj, bool value);
void set_lv_color(lv_obj_t *obj, int color, lv_style_selector_t style = LV_STATE_DEFAULT);
void set_lv_label_color(lv_obj_t *obj, int color, lv_style_selector_t style = LV_STATE_DEFAULT);
String get_color_fixed_6_str(int hexcode);
String insert_color(String text, int color);
#pragma endregion
#endif
