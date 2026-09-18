---
title: Misc and logging
description: Time formatting, printf-style String building, heap and filesystem usage, boot reasons, and the coloured log tags every module prints with.
section: modules
order: 100
---

# Misc — `Core/Misc.h` and `LOGS.h`

Small things every module reaches for. `Misc.h` is deliberately a **leaf**:
`Timers.h`, `NightMareCommand.h` and `NightMareNetwork.h` all include it, so it
must never include WiFi, MQTT or HTTP — which is why `getSystemStatus()` lives
in its own module rather than here. Compiled with `COMPILE_MISC`.

## Time formatting

```cpp
String timestampToDateString(uint32_t timestamp, TimeStampFormat format = DateAndTime);
```

With macros for the common shapes, each in a `String` form and a `.c_str()`
form for `printf`:

| macro | gives |
| --- | --- |
| `TIME(t)` / `TIME_STR(t)` | `HH:MM` |
| `TIME_FULL(t)` | `HH:MM:SS` |
| `DATE(t)` | `DD-MM-YYYY` |
| `DATE_NO_YEAR(t)` | `DD-MM` |
| `DOW_DATE(t)` | `Weekday, DD-MM-YYYY` |
| `TIME_SINCE(t)` | elapsed since `t`, humanised |
| `COUNTDOWN(t)` | remaining until `t` |
| `LIVE_TIME(t)` | `HH:MM` with the colon blinking each second, for displays |

```cpp
uint32_t timestampOfNextOccurrence(String hhmm);   // next time today/tomorrow the clock reads "05:30"
```

That last one is what turns a config value like `ac_turn_off_time = "05:30"`
into an epoch for the [Scheduler](/docs/modules/scheduler).

## Strings and system

```cpp
String      formatString(const char *format, ...);   // printf into a String; FORMAT_BUFFER_SIZE = 1024
float       ramUsagePercent();
float       fsUsagePercent();
const char *getBootReason(int reason);               // esp_reset_reason() code -> name
```

`HOUR` (3600) and `MINUTE` (60) are defined here and in `Timers.h`, both
guarded.

## Logging — `LOGS.h`

ANSI-coloured tags, so a serial monitor separates modules at a glance:

```cpp
Serial.printf("%s%s connected as %s\n", MILLIS_LOG, MQTT_REMOTE_TAG, getDeviceName());
Serial.printf("%s task created\n", OK_LOG(res == pdPASS));   // [OK] or [ERROR]
```

| tag | prints |
| --- | --- |
| `ERR_TAG`, `WARN_TAG`, `INFO_TAG`, `OK_TAG` | `[ERROR]` `[WARNING]` `[INFO]` `[OK]` |
| `OK_LOG(cond)` | `OK_TAG` or `ERR_TAG` by a boolean |
| `ASYNC_TAG`, `SYNC_TAG`, `ASYNC_LOG(cond)` | for the command resolver |
| `TIMER_TAG`, `SCHEDULER_TAG`, `MQTT_REMOTE_TAG`, `MQTT_LOCAL_TAG`, `COMMAND_RESOLVER_TAG`, `WIFI_TAG`, `TCP_TAG` | per module |
| `MILLIS_LOG` | `[123456]` in grey; empty when `COMPILE_MISC` is off |

Each module has its own `*_LOGF` macro over these, compiled out unless
`COMPILE_SERIAL` (global) or the module's own switch is on — `Timers.cpp`
currently forces its own on.
