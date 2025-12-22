#include "TimeSyncronization.h"
#ifdef COMPILE_TIMESYNC

void (*timeSyncCallback)(void) = nullptr;
void _setTime(unsigned long timestamp);

/// @brief Attempts to get the time synced using worldtimeapi.
/// @return True if successful or false otherwise.
bool autoSyncTime()
{
  bool result = false;
#ifdef COMPILE_SERIAL
  Serial.println("Syncing Time Online");
#endif
  WiFiClient client;
  HTTPClient http;
  http.begin(client, API_URL); // HTTP
  int httpCode = http.GET();

  // httpCode will be negative on error
  if (httpCode > 0)
  {
    // HTTP header has been send and Server response header has been handled
    // file found at server
    if (httpCode == HTTP_CODE_OK)
    {
#ifdef COMPILE_SERIAL
      Serial.printf("[HTTP] OK... code: %d\n", httpCode);
#endif
      String payload = http.getString();
      int index_timestamp = payload.indexOf("unixtime: ");
      if (index_timestamp < 0)
        return false;
      int index_timestamp_nl = payload.indexOf("\n", index_timestamp);
      if (index_timestamp_nl < 0)
        return false;
      String timestamp = payload.substring(index_timestamp + 10, index_timestamp_nl);
      unsigned long _timestamp = strtoul(timestamp.c_str(), NULL, 10);
      int index_offset = payload.indexOf("raw_offset: ");
      if (index_offset > 0)
      {
        int index_offset_nl = payload.indexOf("\n", index_offset);
        if (index_offset_nl > 0)
        {
          String offset = payload.substring(index_offset + 11, index_offset_nl);
          int _offset = offset.toInt();
          _timestamp += _offset;
        }
      }
      _setTime(_timestamp);
      result = true;
    }
  }
  else
  {
#ifdef COMPILE_SERIAL
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
#endif
  }
  http.end();
  return result;
}

/// @brief Manually syncs the time to a specific timestamp.
/// @param timestamp The timestamp to set the time to.
void manualSyncTime(unsigned long timestamp)
{
  _setTime(timestamp);
}

/// @brief Internal function to set the time and handle related tasks.
/// @param timestamp The timestamp to set the time to.
void _setTime(unsigned long timestamp)
{
  setTime(timestamp);
  SystemSettings.setFlag("time_synced", true);
  SystemSettings.set("boot_time", String(timestamp - (millis() / 1000)));
  if (timeSyncCallback)
  {
    timeSyncCallback();
  }
#if defined(COMPILE_SCHEDULER) && !defined(SCHEDULER_USE_MILLIS)
  // Notify scheduler of time change
  scheduler.onSync(millis() / 1000);
#endif
}

void onTimeSync(void (*callback)(void))
{
  timeSyncCallback = callback;
}

#endif