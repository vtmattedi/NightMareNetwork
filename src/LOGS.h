#ifndef LOGS_H
#define LOGS_H
/// LOGS.h - Logging macros for NightMare Network

// The MILLIS_LOG Needs Arduino.h for millis() and String and formatString from MISC.h
// if not running MISC
#ifdef COMPILE_MISC
#include <Arduino.h>
extern String formatString(const char *format, ...);
#define MILLIS_LOG formatString("\x1b[90m[%lu]\x1b[0m", millis()).c_str()
#else
#define MILLIS_LOG ""
#endif

#define ERR_LOG "\x1b[91;1m[ERROR]\x1b[0m"
#define WARN_LOG "\x1b[93;1m[WARNING]\x1b[0m"
#define OK_LOG(var) var ? "\x1b[92;1m[OK]\x1b[0m" : "\x1b[91;1m[ERR]\x1b[0m"

#define TIMER_LOG "\x1b[97;1m[Timer]\x1b[0m"
#define SCHEDULER_LOG "\x1b[96;1m[Scheduler]\x1b[0m"
#define MQTT_REMOTE_LOG "\x1b[94;1m[MQTT-Remote]\x1b[0m"
#define MQTT_LOCAL_LOG "\x1b[34;1m[MQTT-Local]\x1b[0m"
#define COMMAND_RESOLVER_LOG "\x1b[35;1m[Command-Resolver]\x1b[0m"
#define WIFI_LOG "\x1b[36;1m[WiFi]\x1b[0m"
#define TCP_LOG "\x1b[32;1m[TCP]\x1b[0m"
#endif