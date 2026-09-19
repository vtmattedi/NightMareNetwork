---
title: Runtime and time
description: Pollers, Jobs, managed execution and the system clock.
section: runtime
order: 1
---

# Runtime and time

`Runtime` drives pollable components. In manual mode, call `runtime.tick()` from Arduino `loop()`. For a device that needs a managed FreeRTOS executor, call `runtime.startManaged(stackBytes, priority, periodMs)` after registering components. The Runtime uses one task for its registered pollers and Scheduler. Do not also call `tick()` manually while managed mode is running.

```cpp
Runtime runtime;
runtime.add([](void* context) {
    static_cast<NightMare::Network*>(context)->tick();
}, &device);
runtime.scheduler().every("heartbeat", 15000, heartbeat, context);

void loop() { runtime.tick(); }
```

A component can expose `tick()` without creating its own task. `WifiStation`, `Console`, `OtaService` and the temperature example's sensor driver follow that policy. The ESP MQTT client manages its transport task internally; it only enqueues messages for `Network::tick`, which calls application handlers on the Runtime thread.

## Jobs

`Scheduler` holds fixed-capacity Jobs (`NIGHTMARE_MAX_JOBS`, 16 by default). A Job combines a schedule, a plain function pointer and context. It is not a network Resource. `after(id, delayMs, fn, ctx)` runs once; `every(id, intervalMs, fn, ctx)` repeats. Both use monotonic `millis()`, so wall-clock corrections do not change elapsed durations.

`dailyAt(id, hourUtc, minuteUtc, fn, ctx)` uses UTC wall-clock time. It waits until the system clock is valid, runs at most once per observed UTC day, and does not run a missed time immediately when first armed after that time. Jobs are currently volatile; persistent configuration is a future topic.

## System clock

`NightMare::Time::now()` reads the platform epoch clock. `second`, `minute`, `hour`, `day`, `month` and `year` extract UTC fields from a supplied epoch or the current time. `Time::valid()` treats dates before 2020-01-01 UTC as unsynchronized. `Time::setEpoch()` sets the system clock.

When MQTT reconnects and the clock is invalid, Network publishes `time` on `Control/request`. It accepts a `Control/time` JSON reply with a Unix `timestamp` in seconds or milliseconds. The canonical clock stays UTC. Timezone and daylight saving policy are future topics; the transport's offset field does not shift the system epoch.

Do not use wall-clock time for intervals or monotonic `millis()` for calendar events.
