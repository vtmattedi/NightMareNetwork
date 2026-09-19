#include "TimeSyncronization.h"

void (*timeSyncCallback)(void) = nullptr;
void _setTime(unsigned long timestamp);
/// @brief Attempts to get the time synced using worldtimeapi.
/// @return True if successful or false otherwise.
bool autoSyncTime()
{
  /*Sample response:
  {
  "utc_iso": "2026-09-08T12:49:00Z",
  "utc_rfc3339": "2026-09-08T12:49:00+00:00",
  "utc_datetime": "2026-09-08T12:49:00.746867+00:00",
  "unix": 1788871740,
  "unix_ms": 1788871740746,
  "day_of_week": 2,
  "day_of_year": 251,
  "week_number": 37,
  "timezone": "America/Bahia",
  "datetime": "2026-09-08T09:49:00.746867-03:00",
  "local_iso": "2026-09-08T09:49:00-03:00",
  "abbreviation": "UTC-03:00",
  "utc_offset": "-03:00",
  "utc_offset_minutes": -180,
  "dst": false,
  "dst_next_transition": null,
  "dst_next_abbreviation": null,
  "dst_next_offset": null
}
  */
  bool result = false;
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
      String payload = http.getString();
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, payload);
      if (error)
      {
        http.end();
        return false;
      }
      if (!doc.containsKey("unix") || !doc["unix"].is<unsigned long>())
      {
        http.end();
        return false;
      }
      unsigned long _timestamp = doc["unix"].as<unsigned long>();
      // TimeLib holds Unix seconds; timezone offsets belong in display formatting.
      _setTime(_timestamp);
      result = true;
    }
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
  SystemState.setFlag("time_synced", true);
  SystemState.set("boot_time", String(timestamp - (millis() / 1000)));
  if (timeSyncCallback)
  {
    timeSyncCallback();
  }
}

void onTimeSync(void (*callback)(void))
{
  timeSyncCallback = callback;
}

