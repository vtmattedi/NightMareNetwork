#include <Core/Misc.h>

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


/// @brief Converts a MQTT status code to its name.
/// @param status The MQTT status.
/// @return A string with the name of the MQTT status.
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
