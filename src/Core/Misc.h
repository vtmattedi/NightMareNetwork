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
#define TIME(var) timestampToDateString(var,TimeStampFormat::OnlyTime)
#define TIME_STR(var) TIME(var).c_str()
#define DATE(var) timestampToDateString(var,TimeStampFormat::OnlyDate)
#define DATE_STR(var) DATE(var).c_str()
#define DATE_NO_YEAR(var) timestampToDateString(var,TimeStampFormat::SmallDate)
#define DATE_NO_YEAR_STR(var) DATE_NO_YEAR(var).c_str()
#define TIME_SINCE(var) timestampToDateString(var,TimeStampFormat::TimeSinceStamp)
#define TIME_SINCE_STR(var) TIME_SINCE(var).c_str()
#define COUNTDOWN(var) timestampToDateString(var,TimeStampFormat::CountdownFromTimestamp)
#define COUNTDOWN_STR(var) COUNTDOWN(var).c_str()


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


/// @brief Sets the background color of a lvgl object.
/// @param obj A pointer to the lvgl object.
/// @param color The hexcode of the color.
/// @param style The target style of the label. [Default = 0]
void set_lv_color(lv_obj_t *obj, int color, lv_style_selector_t style = LV_STATE_DEFAULT)
{
 lv_obj_set_style_bg_color( obj,lv_color_hex(color), style);
}

/// @brief Sets the color of a lvgl label.
/// @param obj A pointer to a lvgl label.
/// @param color The hexcode of the color.
/// @param style The target style of the label. [Default = 0]
void set_lv_label_color(lv_obj_t *obj, int color, lv_style_selector_t style = LV_STATE_DEFAULT)
{
  lv_obj_set_style_text_color(obj, lv_color_hex(color), style);
}

/// @brief Gats the hexcode of a color in 6 digits.
/// @param hexcode The hexcode of the color.
/// @return A String with the hexcode of the color in 6 digits.
String get_color_fixed_6_str(int hexcode)
{
  String auxStr = String(hexcode, HEX);
  while (auxStr.length() < 6)
  {
    auxStr = "0" + auxStr;
  }

  return auxStr;
}

/// @brief Inserts a color in a String in the format #[hexcode] [text]# for lvgl labels.
/// @param text The text to be colored.
/// @param color the hexcpde of the color.
/// @return A String with the color and text.
String insert_color(String text, int color)
{
  String res = "#" + get_color_fixed_6_str(color) +" "+ text + "#";
  return res;
}


#endif
