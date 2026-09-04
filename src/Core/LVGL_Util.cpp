#include <Core/LVGL_Util.h>
#ifdef COMPILE_LVGL

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

#endif /* COMPILE_LVGL */
