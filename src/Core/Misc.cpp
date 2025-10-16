#include <Core/Misc.h>
#ifdef COMPILE_MISC
/// @brief Uses `printf` to format a String object. 
/// @param format The string with the placeholders '%d', '%x' etc.
/// @param args... The args to match the string passed.
/// @return The formatted String.
String formatString(const char *format, ...)
{
    // Create a buffer to store the formatted string
    char buffer[FORMAT_BUFFER_SIZE]; // You can adjust the size as needed
    va_list args;
    va_start(args, format);
    // Format the string into the buffer
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    if (len < 0)
    {
        // Error occurred during formatting
        return String();
    }
    // Convert the formatted buffer to a String
    return String(buffer);
}

/// @brief Converts a timestamp to a date and time String.
/// @param timestamp timestamp The unix timestamp to be parsed, used by TimeLib.h (seconds).
/// @param _format the format you want the String.
/// @return a String with the date on the timestamp formatted as HH:mm DD/MM/YY, attending to the TimeStampFormat passed.
String timestampToDateString(uint32_t timestamp, const TimeStampFormat _format)
{
    // Convert the timestamp to a time_t object.
    time_t timeObject = timestamp;

    // Extract the date components from the time_t object.
    int _year = year(timeObject) % 100;
    int _month = month(timeObject);
    int _day = day(timeObject);

    int _hour = hour(timeObject);
    int _minute = minute(timeObject);
    uint8_t dow = dayOfWeek(timeObject);

    String dateString = "";

    if (_format == CountdownFromTimestamp)
    {
        bool overTimer = timestamp < now();
        if (overTimer)
            dateString += "-";

        int endTime = timestamp - now();
        // Serial.printf("OT: %d| ET: %d | H: %d | M: %d| S: %d\n",
        //               overTimer, endTime, endTime / HOUR, (endTime / MINUTE) % MINUTE, endTime % MINUTE);
        if (overTimer)
            endTime = now() - timestamp;

        if (endTime > HOUR) // >1h
        {
            if (endTime / HOUR < 10)
                dateString += "0";
            dateString += endTime / HOUR;
            dateString += ":";
            if ((endTime / MINUTE) % MINUTE < 10)
                dateString += "0";
            dateString += (endTime / MINUTE) % MINUTE;
        }
        else
        {
            if (endTime / MINUTE < 10)
                dateString += "0";
            dateString += endTime / MINUTE;
            if (endTime % 2 == 0)
                dateString += ":";
            else
                dateString += " ";
            if (endTime % MINUTE < 10)
                dateString += "0";
            dateString += endTime % MINUTE;
        }
        return dateString;
    }

    if (_format == TimeSinceStamp)
    {
        int boottime = now() - timestamp;
        if (boottime < 90)
        {
            dateString += boottime;
            dateString += " sec.";
        }
        else if (boottime < 90 * 60)
        {
            dateString += boottime / 60;
            dateString += " min.";
        }
        else
        {
            dateString += boottime / 3600;
            dateString += " hours.";
        }
        return dateString;
    }

    // Adds Time
    if (_format == OnlyTime || _format == DateAndTime || _format == OnlyTimeLive)
    {
        dateString = String(_hour, DEC).length() == 1 ? "0" + String(_hour, DEC) : String(_hour, DEC);
        if (now() % 2 == 0 && _format == OnlyTimeLive)
            dateString += " ";
        else
            dateString += ":";
        dateString += String(_minute, DEC).length() == 1 ? "0" + String(_minute, DEC) : String(_minute, DEC);
    }
    if (_format == DowDate)
    {
        dateString = dayShortStr(dow);
    }

    // Adds a Space between Time and Date
    if (_format == DateAndTime || _format == DowDate)
        dateString += " ";
    // Adds Date
    if (_format == OnlyDate || _format == DateAndTime || _format == DowDate || _format == SmallDate)
    {
        dateString += String(_day, DEC).length() == 1 ? "0" + String(_day, DEC) : String(_day, DEC);
        dateString += "/";
        dateString += String(_month, DEC).length() == 1 ? "0" + String(_month, DEC) : String(_month, DEC);
        if (_format != DowDate && _format != SmallDate)
        {
            dateString += "/";
            dateString += String(_year, DEC);
        }
    }
    return dateString;
}


#ifdef COMPILE_LVGL
#pragma region "LVGL Helper Functions"

/// @brief Sets or clears a flag to a lvgl object.
/// @param obj A pointer to the lvgl object.
/// @param value The value of the flag.
/// @param flag The flag to be set. [default = hidden flag]
void set_lv_flag(lv_obj_t *obj, bool value, lv_obj_flag_t flag)
{
  if (value)
    lv_obj_add_flag(obj, flag);
  else
    lv_obj_clear_flag(obj, flag);
}


/// @brief Sets or clears a flag to a lvgl object.
/// @param obj A pointer to the lvgl object.
/// @param value The value of the flag.
/// @param flag The flag to be set. [default = hidden flag]
void set_lv_state(lv_obj_t *obj, bool value, lv_state_t state)
{
  if (value)
    lv_obj_add_state(obj, state);
  else
    lv_obj_clear_state(obj, state);
}


/// @brief Sets a lvgl object visible or hidden.
/// @param obj A pointer to the lvgl object.
/// @param value Visible or hidden.
void set_lv_visible(lv_obj_t *obj, bool value)
{
  set_lv_flag(obj, !value, LV_OBJ_FLAG_HIDDEN);
}


/// @brief Sets the background color of a lvgl object.
/// @param obj A pointer to the lvgl object.
/// @param color The hexcode of the color.
/// @param style The target style of the label. [Default = 0]
void set_lv_color(lv_obj_t *obj, int color, lv_style_selector_t style)
{
 lv_obj_set_style_bg_color( obj,lv_color_hex(color), style);
}

/// @brief Sets the color of a lvgl label.
/// @param obj A pointer to a lvgl label.
/// @param color The hexcode of the color.
/// @param style The target style of the label. [Default = 0]
void set_lv_label_color(lv_obj_t *obj, int color, lv_style_selector_t style)
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

#pragma endregion
#endif


#endif