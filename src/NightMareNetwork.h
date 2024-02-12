#include <Arduino.h>
#include <TimeLib.h>

#ifdef LVGL_H
#include <lvgl.h>
#endif

#ifndef NIGHTMARENETWORK
#define NIGHTMARENETWORK

/*-------Templates to be compiled---------*/

// #define  USE_BYTE_TEMPLATE //Compile Byte Template
#define USE_INT_TEMPLATE // Compile int Template
// #define  USE_UINT16_TEMPLATE //Complie uin16_t template
#define USE_UINT32_TEMPLATE // Complie uin32_t template
#define USE_DOUBLE_TEMPLATE // Complie double template
#define USE_BOOL_TEMPLATE   // Complie bool template
// #define  USE_STRING_TEMPLATE //Complie String template

/*---------------------------------------*/

#define ASSERT_DELAY 10000
#define FORMAT_BUFFER_SIZE 1024 // max String length
#define HOUR 3600
#define MINUTE 60
template <typename T>
class ServerVariable
{
public:
  T value;
  bool send_delay = false;
  uint16_t millis_to_delay = 1000;
  uint32_t millis_to_stale = 10 * 60 * 1000; // 10 mins
  void (*on_assert_result)(bool) = NULL;
  void (*on_send)(T) = NULL;
  void (*on_value_changed)() = NULL;
  bool stale = true;
  bool debug = false;
  char *label = (char *)"no label";

private:
  T serverValue;
  bool assert_handled = true;
  bool send_handled = true;
  uint32_t millis_to_assert_server = ASSERT_DELAY;
  uint32_t millis_to_send = 0;
  uint32_t millis_last_server_recieved = 0;

public:
  ServerVariable(T initialValue);
  ServerVariable();
  ServerVariable(char *name);
  void handleServer(T newServerValue);
  void sync();
  void change(T newValue);

private:
  void onValueChanged();
};

extern String formatString(const char *format, ...);
extern const char *mqttStatusToString(int);

#define MAX_TASKS 20

struct TimerTask
{
public:
  String label = "unused";
  bool enable = false;
  bool use_millis = false;
  uint16_t interval = 100;
  uint32_t last_time = 0;
  void (*callback)() = NULL;
  void run();
  void reset();
  uint16_t timeLeft();
};

struct indexresult
{
  bool full = false;
  bool found = false;
  uint8_t index = 0;
};

class TimersHandler
{
public:
  bool debug = false;
  TimerTask _tasks[MAX_TASKS];
  bool create(String label, uint16_t interval, void (*callback)(void), bool use_millis = false);
  indexresult getIndex(String label);
  void run();
  String Timeleft(String label);
};

extern TimersHandler Timers;

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

#endif