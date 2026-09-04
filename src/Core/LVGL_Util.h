/*----------------------------------------------------------*/
///
///@file LVGL_Util.h -
/// Implements LVGL helper functions for NightMare Network.
/// Author: Vitor Mattedi Carvalho
/// Date: 03-09-2026
/// Version: 1.0
///         Extracted from Misc.h.
/*----------------------------------------------------------*/

#pragma once
#ifndef NIGHTMARE_CORE_LVGL_UTIL_H
#define NIGHTMARE_CORE_LVGL_UTIL_H

#include <Modules.config.h>
#ifdef COMPILE_LVGL
#include <Arduino.h>
#include <lvgl.h>

#pragma region "LVGL Helper Functions"
void set_lv_flag(lv_obj_t *obj, bool value, lv_obj_flag_t flag = LV_OBJ_FLAG_HIDDEN);
void set_lv_state(lv_obj_t *obj, bool value, lv_state_t state = LV_STATE_CHECKED);
void set_lv_visible(lv_obj_t *obj, bool value);
void set_lv_color(lv_obj_t *obj, int color, lv_style_selector_t style = LV_STATE_DEFAULT);
void set_lv_label_color(lv_obj_t *obj, int color, lv_style_selector_t style = LV_STATE_DEFAULT);
String get_color_fixed_6_str(int hexcode);
String insert_color(String text, int color);

#pragma endregion
#endif /* COMPILE_LVGL */

#endif
