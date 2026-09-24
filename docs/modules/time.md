---
title: Time
description: Wall-clock access, local formatting, SNTP synchronization, and time status.
section: modules
order: 65
---

# Time

NightMare separates wall-clock time, local formatting, and synchronization. The core clock API lives in `NightMare::Time`; automatic synchronization is an ESP32 platform service.

## Wall clock

```cpp
time_t now = NightMare::Time::now();
bool valid = NightMare::Time::valid();
```

Wall Scheduler jobs require a valid clock. Monotonic jobs do not.

The component accessors are UTC:

```cpp
NightMare::Time::second(epoch);
NightMare::Time::minute(epoch);
NightMare::Time::hour(epoch);
NightMare::Time::day(epoch);
NightMare::Time::month(epoch);
NightMare::Time::year(epoch);
```

## Local formatting

Human-facing formatting uses the process `TZ` setting owned and applied by `DeviceIdentity`:

```cpp
String NightMare::Time::timestampToDateString(
    time_t timestamp,
    TimeStampFormat format = DateAndTime);
```

Current formats are:

```text
DateAndTime
OnlyDate
SmallDate
OnlyTime
OnlyTimeWithSeconds
OnlyTimeLive
DowDate
TimeSinceStamp
CountdownFromTimestamp
```

Convenience functions are:

```cpp
NightMare::Time::timeString(timestamp);
NightMare::Time::fullTimeString(timestamp);
NightMare::Time::dateString(timestamp);
```

Formatting is local-time presentation only. Unix epoch values remain UTC-based timestamps.

## Next local occurrence

```cpp
time_t next =
    NightMare::Time::timestampOfNextOccurrence("21:30");
```

This returns the next local occurrence of `HH:MM`, or `0` if the clock is invalid or the input is malformed. If today's occurrence has already passed, it returns tomorrow's occurrence.

## Timezone and NTP configuration

`DeviceIdentity::begin()` loads the persisted timezone, applies it to the process, and falls back to:

```cpp
NM_TIMEZONE
```

Current default:

```text
UTC0
```

Automatic SNTP uses:

```cpp
NM_NTP_SERVER_1
NM_NTP_SERVER_2
NM_NTP_SERVER_3
```

with current defaults:

```text
pool.ntp.org
time.nist.gov
time.google.com
```

Projects can override these values in `NightMareConfig.h`.

The runtime identity API is:

```cpp
gDeviceIdentity.getTimezone();
gDeviceIdentity.setTimezone("EST5EDT,M3.2.0,M11.1.0");
```

The setter persists and applies the timezone immediately. A timezone is a non-empty printable POSIX `TZ` string up to 128 characters.

## Automatic SNTP synchronization

Canonical startup API:

```cpp
bool startSntpTimeSync();
```

It requires WiFi to be connected, configures the ESP32 SNTP client, and returns without waiting for synchronization.

The standard ESP lifecycle starts SNTP on the first successful WiFi connection when `NM_ENABLE_TIME_SYNC` is enabled.

## Completion dispatch

The ESP SNTP callback runs on lwIP's task. NightMare therefore does not update
the framework flag or call application callbacks directly from that callback.

Instead it records a pending event. `tickNightMareESP()` calls:

```cpp
processTimeSyncEvents();
```

which applies the completed sync in the normal cooperative context.

Applications using automatic time synchronization should therefore continue calling `tickNightMareESP()` even when the Scheduler itself runs in TASK mode.

## Runtime state and callback

Successful synchronization records:

```text
SystemState.set(SystemFlag::TimeSynced)
```

and invokes the optional callback registered through:

```cpp
onTimeSync(callback);
```

For SNTP, that callback runs when the pending event is serviced by `tickNightMareESP()`.

## Manual and MQTT-assisted synchronization

An explicit timestamp can be applied with:

```cpp
manualSyncTime(timestamp);
```

The existing MQTT control path remains available:

```text
Control/request
Control/time
```

When MQTT connects with an invalid wall clock, NightMare may request a timestamp. A valid `Control/time` response is applied through `manualSyncTime()`.

SNTP is the normal automatic synchronization path; the MQTT control topics are an auxiliary path.

## TIME command

```text
TIME
TIME STATUS
```

Both forms return the same JSON status document with:

```text
synced
valid
epoch
local
timezone
uptime_ms
```

`valid` reports whether the wall clock is usable. `synced` is true only when the clock is valid and NightMare has recorded a synchronization event. `local` uses the configured process timezone.

Timezone has its own query and mutation commands:

```text
TIMEZONE
TIMEZONE SET <posix-tz>
CHANGE TIMEZONE <posix-tz>
```

Quote the timezone when it contains spaces. The response is JSON containing the active `timezone`.

## Relationship to Scheduler

The Scheduler has two clock domains:

```text
Wall
    Unix seconds; requires valid wall clock

Monotonic
    millis(); independent of time synchronization
```

Use wall scheduling for civil/absolute time and monotonic scheduling for delays and boot-relative intervals.
