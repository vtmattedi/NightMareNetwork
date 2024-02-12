#include "NightMareNetwork.h"

template <typename T>
ServerVariable<T>::ServerVariable(T initialValue)
{
    value = initialValue;
}
template <typename T>
ServerVariable<T>::ServerVariable()
{
}
template <typename T>
ServerVariable<T>::ServerVariable(char *name)
{
    label = name;
}
template <typename T>
void ServerVariable<T>::handleServer(T newServerValue)
{
    serverValue = newServerValue;
    stale = false;
    millis_last_server_recieved = millis();
    if (debug)
    {
        Serial.printf("[%s]...new Server value: [%s] client value is: [%s], assert is: %s...\n", label, String(newServerValue).c_str(), String(value).c_str(), assert_handled ? "Handled" : "Waiting");
    }

    if (assert_handled)
    {
        if (value != newServerValue)
        {
            value = newServerValue;
            onValueChanged();
        }
    }
}
template <typename T>
void ServerVariable<T>::sync()
{
    if (millis() - millis_last_server_recieved > millis_to_stale && !stale)
    {
        stale = true;
        onValueChanged();
        if (debug)
            Serial.printf("[%s]...Variable Stale...\n", label);
    }
    if (millis() > millis_to_assert_server && !assert_handled)
    {
        bool equal = value == serverValue;
        if (debug)
            Serial.printf("[%s]...Server Variable asserted server is: [%s] client is :[%s]...\n", label, String(serverValue).c_str(), String(value).c_str());
        if (!equal)
        {
            value = serverValue;
            onValueChanged();
        }
        if (on_assert_result)
        {
            (*on_assert_result)(equal);
        }
        assert_handled = true;
    }
    if (!send_delay)
        return;
    if (millis() > millis_to_send && !send_handled)
    {
        if (on_send)
        {
            (*on_send)(value);
            if (debug)
                Serial.printf("[%s][%d] delay ended\n", label, millis());
        }
        send_handled = true;
    }
}
template <typename T>
void ServerVariable<T>::change(T newValue)
{
    bool trigger = false;
    // if (debug)
    //     Serial.printf("[%s], [%s], [%d]\n", String(value).c_str(), String(newValue).c_str(), newValue != value);
    if (value != newValue)
    {
        trigger = true;
        value = newValue;
    }

    if (send_delay)
    {
        millis_to_send = millis() + millis_to_delay;
        send_handled = false;
        Serial.printf("[%s] [%d] delay Started\n", label, millis());
    }

    millis_to_assert_server = millis() + ASSERT_DELAY;
    assert_handled = false;
    if (debug)
        Serial.printf("[%s]...new Client value: [%s] time to assert:[%d], trigger is: %s...\n", label, String(value).c_str(), millis_to_assert_server, assert_handled ? "True" : "False");

    if (trigger)
        onValueChanged();
}
template <typename T>
void ServerVariable<T>::onValueChanged()
{
    if (on_value_changed)
    {
        (*on_value_changed)();
    }
}
#ifdef USE_INT_TEMPLATE
template class ServerVariable<int>;
#endif
#ifdef USE_UINT32_TEMPLATE
template class ServerVariable<uint32_t>;
#endif
#ifdef USE_UINT16_TEMPLATE
template class ServerVariable<uint32_t>;
#endif
#ifdef USE_BYTE_TEMPLATE
template class ServerVariable<uint8_t>;
#endif
#ifdef USE_DOUBLE_TEMPLATE
template class ServerVariable<double>;
#endif
#ifdef USE_STRING_TEMPLATE
template class ServerVariable<String>;
#endif
#ifdef USE_BOOL_TEMPLATE
template class ServerVariable<bool>;
#endif

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

TimersHandler Timers = TimersHandler();

void TimerTask::run()
{
    if (!enable)
        return;

    uint32_t _now = millis();
    if (!use_millis)
        _now = now();

    if (_now - last_time >= interval)
    {
        last_time = _now;
        if (callback)
        {
            (*callback)();
        }
    }
}

void TimerTask::reset()
{
    label = "unused";
    enable = false;
    use_millis = false;
    interval = 100;
    last_time = 0;
    callback = NULL;
}

uint16_t TimerTask::timeLeft()
{
    uint32_t _now = now();
    if (use_millis)
        _now = millis();

    return _now - last_time;
}

bool TimersHandler::create(String label, uint16_t interval, void (*callback)(void), bool use_millis)
{
    indexresult res = getIndex(label);
    if (res.full || res.found)
        return false;
    if (res.index < MAX_TASKS)
    {
        _tasks[res.index].callback = callback;
        _tasks[res.index].label = label;
        _tasks[res.index].interval = interval;
        _tasks[res.index].use_millis = use_millis;
        _tasks[res.index].enable = true;
        Serial.printf("...timer [%s] created at [%d]...\n", label.c_str(), res.index);
        return true;
    }
    return false;
}

indexresult TimersHandler::getIndex(String label)
{
    indexresult res;
    for (size_t i = 0; i < MAX_TASKS; i++)
    {
        if (_tasks[i].label == label)
        {
            res.found = true;
            res.index = i;
            return res;
        }
    }
    res.full = true;
    for (size_t i = 0; i < MAX_TASKS; i++)
    {
        if (_tasks[i].label == "unused")
        {
            res.index = i;
            res.full = false;
            res.found = false;
            return res;
        }
    }
    return res;
}

void TimersHandler::run()
{
    for (size_t i = 0; i < MAX_TASKS; i++)
    {
        _tasks[i].run();
    }
}

String TimersHandler::Timeleft(String label)
{
     indexresult res = getIndex(label);
     if (res.found)
     return String(_tasks[res.index].timeLeft());
     
     return "There is no task with such label";
     
}

const char *mqttStatusToString(int status)
{
#define MQTT_CONNECTION_TIMEOUT -4
#define MQTT_CONNECTION_LOST -3
#define MQTT_CONNECT_FAILED -2
#define MQTT_DISCONNECTED -1
#define MQTT_CONNECTED 0
#define MQTT_CONNECT_BAD_PROTOCOL 1
#define MQTT_CONNECT_BAD_CLIENT_ID 2
#define MQTT_CONNECT_UNAVAILABLE 3
#define MQTT_CONNECT_BAD_CREDENTIALS 4
#define MQTT_CONNECT_UNAUTHORIZED 5
    switch (status)
    {
    case MQTT_CONNECTION_TIMEOUT:
        return "MQTT_CONNECTION_TIMEOUT";
    case MQTT_CONNECTION_LOST:
        return "MQTT_CONNECTION_LOST";
    case MQTT_CONNECT_FAILED:
        return "MQTT_CONNECT_FAILED";
    case MQTT_DISCONNECTED:
        return "MQTT_DISCONNECTED";
    case MQTT_CONNECTED:
        return "MQTT_CONNECTED";
    case MQTT_CONNECT_BAD_PROTOCOL:
        return "MQTT_CONNECT_BAD_PROTOCOL";
    case MQTT_CONNECT_BAD_CLIENT_ID:
        return "MQTT_CONNECT_BAD_CLIENT_ID";
    case MQTT_CONNECT_UNAVAILABLE:
        return "MQTT_CONNECT_UNAVAILABLE";
    case MQTT_CONNECT_BAD_CREDENTIALS:
        return "MQTT_CONNECT_BAD_CREDENTIALS";
    case MQTT_CONNECT_UNAUTHORIZED:
        return "MQTT_CONNECT_UNAUTHORIZED";
    default:
        return "UNKNOWN_STATUS";
    }
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
  if (_format == OnlyDate || _format == DateAndTime || _format == DowDate)
  {
    dateString += String(_day, DEC).length() == 1 ? "0" + String(_day, DEC) : String(_day, DEC);
    dateString += "/";
    dateString += String(_month, DEC).length() == 1 ? "0" + String(_month, DEC) : String(_month, DEC);
    if (_format != DowDate)
    {
      dateString += "/";
      dateString += String(_year, DEC);
    }
  }
  return dateString;
}
