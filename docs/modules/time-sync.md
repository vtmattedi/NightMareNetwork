---
title: Time sync
description: How a device gets the time from the network over MQTT, the optional HTTP fallback, and what depends on the clock being right.
section: modules
order: 90
---

# Time sync — `Core/TimeSyncronization.h`

A device has no RTC and no NTP. Its clock comes from the network: the backend
answers `Control/request` = `time` on `Control/time`, and the MQTT client sets
the clock from that inside its own handler. Compiled with `COMPILE_TIMESYNC`.

## API

```cpp
void manualSyncTime(unsigned long timestamp);   // set the clock; fires onTimeSync callbacks
void onTimeSync(void (*callback)(void));

#ifdef COMPILE_AUTOTIMESYNC
bool autoSyncTime();                             // HTTP GET to a time API; blocking
#endif
```

Time is TimeLib's: `now()` is epoch seconds, `hour()`/`minute()` follow, and
`GMT` from `creds.h` is the offset applied. `Core/Misc.h` has the formatters
(`TIME_STR(now())` → `"HH:MM"`, and the rest).

## The two flags

| flag | gives | costs |
| --- | --- | --- |
| `COMPILE_TIMESYNC` | `manualSyncTime`, `onTimeSync`, the MQTT path | nothing extra |
| `COMPILE_AUTOTIMESYNC` | `autoSyncTime()` over HTTP (`utctime.app`) | `HTTPClient`, and a blocking GET of several seconds on every first connect |

A device on the NightMare network keeps the first and drops the second: the
backend answers within a second, and the library's bootstrap timer keeps
asking every 60 s until `time_synced` is set. `COMPILE_AUTOTIMESYNC` without
`COMPILE_TIMESYNC` is a build error.

## What waits on the clock

- `SystemSettings` flag `time_synced`; `boot_time` is back-computed when the
  first sync arrives, so `BOOTINFO` is right even for a device that booted
  before the network was up.
- The [Scheduler](/docs/modules/scheduler): tasks armed before sync hold
  times in the wrong epoch, and `onSync()` shifts them by the difference.
- Anything comparing `now()` to a wall-clock time. Until `time_synced`,
  `now()` counts from 1970 plus uptime.

## The wire

```
→ Control/request   time                         (raw topic; MQTT_Send_Raw)
← Control/time      {"timestamp": 1758182200000, "offset": 0}
```

Consumed by the library before any application callback sees it. A device
never parses `Control/time` itself.
