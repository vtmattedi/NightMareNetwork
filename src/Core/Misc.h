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

#ifdef LVGL_H
#include <lvgl.h>
#endif

#define HOUR 3600
#define MINUTE 60

#define FORMAT_BUFFER_SIZE 1024 // max String length

enum TimeStampFormat
{
  DateAndTime,
  OnlyDate,
  OnlyTime,
  OnlyTimeLive,
  DowDate,
  TimeSinceStamp,
  CountdownFromTimestamp
};

String timestampToDateString(uint32_t timestamp, const TimeStampFormat _format = DateAndTime);
String formatString(const char *format, ...);
const char *mqttStatusToString(int);

#ifdef LVGL_H

void set_lv_flag(lv_obj_t *obj, bool value, lv_obj_flag_t flag = LV_OBJ_FLAG_HIDDEN)
{
  if (value)
    lv_obj_add_flag(obj, flag);
  else
    lv_obj_clear_flag(obj, flag);
}

void set_lv_state(lv_obj_t *obj, bool value, lv_state_t state = LV_STATE_CHECKED)
{
  if (value)
    lv_obj_add_state(obj, state);
  else
    lv_obj_clear_state(obj, state);
}

void set_lv_visible(lv_obj_t *obj, bool value)
{
  set_lv_flag(obj, !value, LV_OBJ_FLAG_HIDDEN);
}

#endif
