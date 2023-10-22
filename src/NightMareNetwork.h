#include <Arduino.h>

#include <TimeLib.h>

#ifndef NIGHTMARENETWORK
#define NIGHTMARENETWORK

/*-------Templates to be compiled---------*/

//#define  USE_BYTE_TEMPLATE //Compile Byte Template
#define  USE_INT_TEMPLATE //Compile int Template
//#define  USE_UINT16_TEMPLATE //Complie uin16_t template
#define  USE_UINT32_TEMPLATE //Complie uin32_t template
#define  USE_DOUBLE_TEMPLATE //Complie double template
#define  USE_BOOL_TEMPLATE //Complie bool template
//#define  USE_STRING_TEMPLATE //Complie String template

/*---------------------------------------*/

#define ASSERT_DELAY 10000
#define FORMAT_BUFFER_SIZE 1024 //max String length
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

private:
  T serverValue;
  bool assert_handled = true;
  bool send_handled = true;
  uint32_t millis_to_assert_server = 0;
  uint32_t millis_to_send = 0;
  uint32_t millis_last_server_recieved = 0;

public:
  ServerVariable(T initialValue);
  ServerVariable();
  void handleServer(T newServerValue);
  void sync();
  void change(T newValue);

private:
  void onValueChanged();
};

extern String formatString(const char *format, ...);


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
};

extern TimersHandler Timers;




#endif