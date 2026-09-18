---
title: Services
description: Client-side proxies for the AC, light and RGB-light controllers on other devices — the classes a panel or a peer device uses to drive them.
section: modules
order: 60
---

# Services — `src/Services/`

A Service is a client-side proxy for a controller that runs on **another**
device. It holds that controller's state in
[ServerVariables](/docs/modules/server-variables), publishes commands to the
device's console, and parses the device's `state` document back into its
variables. The Dashboard is built on three of them.

The Service *defines* the vocabulary its device must speak: the command words
it sends and the field names it reads. A change on either side is a change on
both. That contract, per Service, is in
[Actuators, controllers and Services](/docs/protocols/actuators-controllers).

Compiled with `COMPILE_SERVICES` plus one switch per Service:
`COMPILE_ACCONTROLLER`, `COMPILE_LIGHTCONTROLLER`,
`COMPILE_LIGHTCOLORCONTROLLER`. They need `COMPILE_SERVERVARIABLES` and the
`int`, `uint32_t`, `double` and `bool` templates.

> **Naming.** The library's `AcController` class is a Service — it *drives* an
> AC device. A device's own `AcController` is a *controller* — it *is* the AC
> device. Same name, opposite sides of the wire. The design document proposes
> renaming the library classes `*Service`; until then, read them as such.

## `ServicesCore.h`

What every Service includes:

```cpp
enum HWID { ACTEMP_ID, HWSLEEP_ID, SWSLEEP_ID, ACTARGET_ID, DOOR_ID, SLEEPIN_ID,
            LIGHTSTATE_ID, RESTORE_ID, HEXCOLOR_ID, BRIGHTNESS_ID };

extern void Send_to_MQTT(String topic, String payload);            // provided by Core/MQTT
extern void FormatSend(String topic, String payload, String host);  // "<host>" + topic -> Send_to_MQTT
```

`HWID` is the `userid` each `ServerVariable` carries into the shared
`on_send_with_info` callback, so one function can format `SETTEMP` for one
variable and `TARGET` for another.

## `AcController(hostname)`

```cpp
AcController Ac("Adler");     // the device name, case-sensitive

Ac.SetAcTemperature(24);      // -> "SETTEMP 24"
Ac.SetAcPower(true);          // -> "POWER 1"
Ac.SetAcTarget(23.5);         // -> "TARGET 23.5"   (negative disables)
Ac.ToggleAcTarget(); Ac.Toggle(); Ac.Shutdown();
Ac.ManualSync(24, true);      // -> "manualsync 1 24"  corrects belief, sends no IR
Ac.SetSleepIn(true);          // -> "SLEEP-IN 1"
Ac.SendRawCommand("POWER");   // -> "SENDIR POWER"
Ac.SetDoorPause(true);        // -> "PAUSEDOORSENSOR 1"

Ac.ParseServerState(json);    // from "<Adler>/state"
Ac.Sync();                    // from loop()
Ac.On_any_value_changed(cb);

Ac.GetState();                // AcState enum, AcStates.h
Ac.CurrentTarget(); Ac.AcTargetEnabled(); Ac.IsStale();
Ac.GetDoorOpen(); Ac.GetDoorPause(); Ac.GetAcPausedByDoorSensor(); Ac.GetSleepTime();
```

Variables: `AcTemperature` (int, negative = off), `AcTarget` (double,
negative = disabled), `HwSleep`, `SWSleep`, `doorOpen`, `sleepIn`, `DoorState`
(bitfield). The sign conventions are load-bearing — the Service derives on/off
from them.

## `LightController(hostname)`

```cpp
LightController Light("Mycroft");
Light.SetLight(true); Light.ToggleLight();   // -> "<host>/Light" = "1"/"0"
Light.ToggleForce();                         // -> "toggle-force", pauses automation 2 h
Light.RestoreAutomations();                  // -> "auto"
Light.ParseServerState(json);                // {"State": bool, "Automation": ts|0}
```

## `LightColorController(hostname)`

Adds colour and brightness on `<host>/Color` and `<host>/Brightness`; state
adds `"Color"` and `"Brightness"`.

## The hostname is a pointer

A Service captures the `const char *` it is constructed with and keeps it
forever. The Dashboard therefore constructs each Service against a fixed
buffer (`char TargetHostAc[48]`) and re-points the panel at a different device
by rewriting the buffer in place, from config — never by constructing a new
Service. Do the same: a device name is runtime configuration.

## Only sync a bound Service

`Sync()` on a Service whose hostname is empty can flush a pending change
through `FormatSend` with an empty host, publishing to `/console/in` — a topic
that belongs to nobody. Guard it:

```cpp
if (Targets_configured(TARGET_SLOT_AC)) Ac.Sync();
```
