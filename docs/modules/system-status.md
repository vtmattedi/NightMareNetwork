---
title: System status
description: The one JSON document that answers "is this device healthy" — published as telemetry, returned by SYSTEMINFO — and why it lives in its own module.
section: modules
order: 120
---

# System status — `Core/SystemStatus.h`

```cpp
String getSystemStatus();
```

One function, one document. It is what `<Device>/telemetry` carries every
15 s and what `SYSTEMINFO` returns on the console. The fields, and how the
backend and Dashboard read them, are in
[Telemetry and identity](/docs/protocols/telemetry). Compiled with
`COMPILE_SYSTEMSTATUS`.

## Why it is its own module

`getSystemStatus()` reads from WiFi, MQTT, HTTP, Configs and the async
command system — it sits at the *top* of the dependency stack. `Misc.h`,
where it would naturally go, is a *leaf* that `Timers.h`,
`NightMareCommand.h` and `NightMareNetwork.h` all include. A leaf that
reaches upward creates an include cycle.

So the header declares nothing but the function, and every heavy include is
confined to `SystemStatus.cpp`, which nothing includes. That is the pattern
for anything else that aggregates across modules: a header with one
declaration, a `.cpp` with the includes.

## Shape stays fixed across builds

A module that is not compiled in reports a disabled value rather than
vanishing from the document — `"mqtt_connection": "Disabled"`,
`"ASYNC_enabled": false` — so a consumer parsing telemetry sees the same
keys from every device regardless of its `Modules.config.h`.

## Required with Timers

`TimersHandler`'s constructor registers the telemetry timer unconditionally
(`BOOTSTRAP_TIMER_SYNC`), and that timer calls `getSystemStatus()`. A build
with `COMPILE_TIMERS` and without `COMPILE_SYSTEMSTATUS` fails in
`Timers.cpp` with an undeclared function — the first thing a project with an
older `Modules.config.h` hits after updating the library.
