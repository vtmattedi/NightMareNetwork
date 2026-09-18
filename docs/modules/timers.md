---
title: Timers
description: Interval callbacks polled from loop(), one-shot timeouts, and the two timers the library creates for itself.
section: modules
order: 20
---

# Timers — `Core/Timers.h`

Cooperative interval callbacks. Nothing here is a hardware timer: `Timers.run()`
is called from `loop()`, checks each task's elapsed time, and calls the ones
that are due, on the loop task. That makes a callback safe to touch anything
`loop()` can touch — and means a callback that blocks blocks everything.
Compiled with `COMPILE_TIMERS`.

## API

```cpp
extern TimersHandler Timers;

bool   Timers.create(String label, uint16_t interval, void (*callback)(void), bool use_millis = false);
String Timers.setTimeout(void (*callback)(void), uint16_t interval, bool use_millis = false);
bool   Timers.cancelTimeout(String label);
bool   Timers.deleteTask(String label);
String Timers.timeleft(String label);
void   Timers.run();
```

- **`interval` is in seconds by default.** Pass `use_millis = true` (or the
  `USE_MS` alias) for milliseconds. `uint16_t`, so the ceiling is 65 535 s or
  ms.
- `create` is idempotent on the label: creating a task whose label exists
  returns false and leaves the original.
- `setTimeout` is one-shot; it self-deletes after firing and returns the label
  it was given so it can be cancelled.
- `TIMER_MAX_TASKS` is 20, including the library's own two.
- Callbacks are plain function pointers. A **captureless lambda** converts to
  one; a capturing lambda does not.

```cpp
Timers.create("sensors", 60, []() { publishSensors(); });          // every minute
Timers.create("ir_pump", 5,  handleIrAsync, true);                 // every 5 ms
Timers.setTimeout([]() { digitalWrite(LED, LOW); }, 5000, true);   // once, in 5 s
```

## The two the library creates

`TimersHandler`'s constructor (`BOOTSTRAP_TIMER_SYNC`, always on) registers:

| label | interval | what |
| --- | --- | --- |
| `sync_timer` | 60 s | asks for time over `Control/request` until `time_synced` is set, then deletes itself |
| `telemetry` | 15 s | `MQTT_Send("/telemetry", getSystemStatus())` |

The telemetry timer is why a device must not add its own — several did, and
published twice. It is also why `COMPILE_SYSTEMSTATUS` is effectively required
alongside `COMPILE_TIMERS`: the timer calls `getSystemStatus()`, which only
exists with that switch.

## What a timer is for, and what it is not

A timer is the right tool for periodic work that completes quickly: publish a
report, poll a non-blocking state machine, pump a queue. It is the wrong tool
for two things:

- **Anything that blocks.** A 750 ms DS18B20 conversion in a timer callback
  holds `loop()` for 750 ms every tick. Use a FreeRTOS task and sleep through
  it, or a non-blocking request-then-read-next-tick pattern.
- **Wall-clock events.** "Turn the AC off at 05:30" is not an interval. A
  timer comparing `TIME_STR(now())` to a string every minute works, but the
  [Scheduler](/docs/modules/scheduler) does it with persistence, a console to
  edit it, and re-alignment on time sync.

## Console

`TIMERS` lists every task with its interval and time left.
