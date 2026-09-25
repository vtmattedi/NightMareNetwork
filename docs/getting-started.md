---
title: Getting started
description: Build a minimal NightMare Network device using the current ESP32 API.
section: getting-started
order: 10
---

# Getting started

This guide uses the current `src/` API.

Do not use the older `Esp32Device`, `ResourceManager`, `NetEvent`, or `NetAction<T>` examples as a guide for the current architecture. The active API is built around the global NightMare services plus `Managed*` / `Remote*` Resources.

## Requirements

The current implementation targets:

```text
framework: Arduino
platform:  espressif32
```

The library currently declares these dependencies:

```text
ArduinoJson ^7.0.0
ArduinoOTA
```

With the default WiFi/MQTT feature set, a normal project needs:

```text
include/creds.h
```

A project may optionally provide:

```text
include/NightMareConfig.h
include/NightMareHardware.h
```

## Add the library

With PlatformIO, depend directly on the repository:

```ini
lib_deps =
    https://github.com/vtmattedi/NightMareNetwork.git
```

During development against a local checkout, a project can instead use a local/symlink dependency.

The public umbrella headers are:

```cpp
#include <NightMare.h>
```

or the compatibility umbrella:

```cpp
#include <NightMareNetwork.h>
```

`NightMareNetwork.h` currently includes `NightMare.h`.

## Configure features

Create:

```text
include/NightMareConfig.h
```

A useful explicit default configuration is:

```cpp
#pragma once

#define NM_FIRMWARE_VERSION "1.0.0"

#define NM_ENABLE_SETTINGS 1
#define NM_ENABLE_RESOURCES 1
#define NM_ENABLE_NETWORK 1
#define NM_ENABLE_CONSOLE 1
#define NM_ENABLE_WIFI 1
#define NM_ENABLE_MQTT 1
#define NM_ENABLE_TELEMETRY 1
#define NM_ENABLE_SCHEDULER 1
#define NM_ENABLE_JOBS 1
#define NM_ENABLE_TIME_SYNC 1

#define NM_TIMEZONE "UTC0"
#define NM_NTP_SERVER_1 "pool.ntp.org"
#define NM_NTP_SERVER_2 "time.nist.gov"
#define NM_NTP_SERVER_3 "time.google.com"

#define NM_ENABLE_OTA 0
#define NM_ENABLE_HTTP 0
#define NM_ENABLE_WEBSOCKET 0
#define NM_ENABLE_LVGL 0

#define NM_ENABLE_ACTION_PAYLOAD_ASSERTION 0
#define NM_ENABLE_REMOTE_RESOURCE_VERIFICATION 1
#define NM_DEFAULT_MANIFEST_FORMAT json

#define NM_TELEMETRY_INTERVAL_MS 60000UL
#define NM_NETWORK_TELEMETRY_INTERVAL_MS 300000UL
#define NM_IDENTITY_CLEANUP_RETRY_MS 60000UL
#define NM_SYSTEM_REQUEST_RETRY_MS 1000UL
#define NM_SYSTEM_REQUEST_MAX_RETRY_MS 300000UL

#define NM_SCHEDULER_OWN_TASK 1

#define NM_CONSOLE_BUILTINS 1
#define NM_CONSOLE_SERIAL 0

#define NM_LOG_LEVEL 0
```

`Features.h` supplies defaults for these macros, so `NightMareConfig.h` is optional. Providing it gives the consuming firmware one explicit place to record its feature choices and firmware version.

## Configure WiFi and MQTT credentials

With the default WiFi/MQTT features enabled, create:

```text
include/creds.h
```

The current platform code expects:

```cpp
#pragma once

#define MQTT_CREDS_H

#define DEFAULT_SSID "your-wifi"
#define DEFAULT_PASSWORD "your-password"

#define LOCAL_MQTT_HOST "192.168.1.10"
#define LOCAL_MQTT_PORT 1883

#define REMOTE_MQTT_URL "mqtt.example.com"
#define REMOTE_MQTT_PORT 8883

#define MQTT_USER "username"
#define MQTT_PASSWD "password"

static const char ROOT_CA[] = R"EOF(
-----BEGIN CERTIFICATE-----
...
-----END CERTIFICATE-----
)EOF";
```

Do not include `mqtt://` in `LOCAL_MQTT_HOST`.

Do not include `mqtts://` in `REMOTE_MQTT_URL`.

The transport builds those URI schemes itself.

Remote MQTT uses TLS and `ROOT_CA`.

Local MQTT currently uses plain:

```text
mqtt://
```

## Optional hardware profile

To make INFO report the board and to publish physical topology, create:

```text
include/NightMareHardware.h
```

Example:

```cpp
#pragma once

#include <NightMare/HardwareProfile.h>

namespace NMHardware
{
inline Profile projectProfile()
{
    static const Board boards[] = {
        {"main", "esp32-c3-supermini:v1"},
        {"front", "status-panel:v1"},
    };
    static const Device devices[] = {
        {"button", "momentary-switch", 0},
        {"status", "LED", 1, DeviceKind::Led, "5mm-tht"},
    };
    static const Net nets[] = {
        {"button", SignalType::Gpio, NoBus, Direction::Input, Pull::Up, true},
        {"status_led", SignalType::Gpio, NoBus, Direction::Output},
    };
    static const Connection connections[] = {
        {{EndpointKind::Board, 0, "GPIO9"},
         {EndpointKind::Device, 0, "1"}, 0},
        {{EndpointKind::Board, 0, "GPIO8"},
         {EndpointKind::Board, 1, "LED"}, 1, 1},
        {{EndpointKind::Board, 1, "LED"},
         {EndpointKind::Device, 1, "A"}, 1},
    };

    return {
        0,
        boards, sizeof(boards) / sizeof(boards[0]),
        devices, sizeof(devices) / sizeof(devices[0]),
        nets, sizeof(nets) / sizeof(nets[0]),
        connections,
        sizeof(connections) / sizeof(connections[0])
    };
}
}
```

If this file is absent, NightMare reports:

```text
boards: [{ id: main, model: unspecified }]
devices: none
nets: none
connections: none
```

`hostBoard` identifies the board running this firmware; array position zero has
no implicit meaning. Board IDs identify physical PCB/module instances, and
board models select stable definitions. Devices are chips/components mounted
on boards. Nets identify common electrical conductors, while Connections make
every physical segment and board crossing explicit. A non-zero connection
group marks conductors bundled in one cable without electrically joining them.
Device `kind` and `form` are optional visualization hints, so the three-field
declaration remains valid. Kinds stay broad (`Sensor`, `Led`, `Button`); model
and a stable lowercase form slug carry specific identity and package shape.

## Declare Resources

Resources should normally have application lifetime.

A typical device can declare them globally:

```cpp
#include <Arduino.h>
#include <NightMare.h>

ManagedSensor<float> temperature("temperature");
ManagedState<bool> power("power");
ManagedAction identify("identify");
```

These mean:

```text
temperature
    this device owns a read-only float Value

power
    this device owns a remotely writable bool Value

identify
    this device implements an Action
```

## Implement a writable state

A `ManagedState<T>` receives already-decoded values.

```cpp
bool onPowerWrite(
    ManagedState<bool> &state,
    const bool &requested)
{
    digitalWrite(8, requested ? HIGH : LOW);

    // true means the requested value is accepted as authoritative state.
    return true;
}
```

Attach it before startup:

```cpp
power.onWrite = onPowerWrite;
```

Returning `false` rejects the request and leaves Resource state unchanged.

## Implement an Action

A ManagedAction handler receives the canonical payload String and returns an `ActionResult`.

```cpp
ActionResult onIdentify(
    ManagedAction &action,
    const String &payload)
{
    (void)action;

    if (payload.length() != 0)
        return {false, "identify takes no arguments"};

    Serial.println("I am here");
    return {true, "OK"};
}
```

Attach it:

```cpp
identify.onInvoke = onIdentify;
```

For richer Actions, declare `ActionArgMetadata` and parse the payload in the handler.

## Bind Resources

Bind every Resource before starting the framework:

```cpp
gResourcesManager.bindResource(&temperature);
gResourcesManager.bindResource(&power);
gResourcesManager.bindResource(&identify);
```

`ResourcesManager` does not own those objects. They must remain alive while bound.

Binding before framework startup also matters for identity cleanup: retained state under an old adopted identity can only be removed for Resources that the current firmware has declared.

## Give Managed Values an initial state

A Managed Value has no authoritative state until it has been set.

Initialize values before networking if appropriate:

```cpp
temperature.setValue(0.0f);
power.setValue(false);
```

Local Managed state does not depend on MQTT being connected.

Once MQTT connects, NightMare re-announces every Managed Value that has authoritative state.

## Start NightMare

A minimal setup is:

```cpp
void setup()
{
    Serial.begin(115200);

    pinMode(8, OUTPUT);

    power.onWrite = onPowerWrite;
    identify.onInvoke = onIdentify;

    gResourcesManager.bindResource(&temperature);
    gResourcesManager.bindResource(&power);
    gResourcesManager.bindResource(&identify);

    temperature.setValue(0.0f);
    power.setValue(false);

    startNightMareESP();
}
```

`startNightMareESP()` coordinates the enabled framework services.

With the normal feature set it:

```text
initializes DeviceIdentity
starts the Scheduler
installs pending identity-cleanup retry
starts periodic telemetry scheduling
starts WiFi_Auto()
```

MQTT is started by the first successful WiFi connection rather than directly by `startNightMareESP()`.

## Service the framework loop

Call:

```cpp
tickNightMareESP();
```

from `loop()`:

```cpp
void loop()
{
    tickNightMareESP();

    // Application cooperative work.
}
```

With the default:

```cpp
NM_SCHEDULER_OWN_TASK 1
```

the Scheduler owns its own FreeRTOS task.

`tickNightMareESP()` therefore does not also tick it.

If configured with:

```cpp
NM_SCHEDULER_OWN_TASK 0
```

the same `tickNightMareESP()` call services the Scheduler cooperatively.

## Publish sensor readings

When the application obtains a new measurement:

```cpp
void publishTemperature(float value)
{
    temperature.setValue(value);
}
```

For a bound ManagedSensor, NightMare publishes the encoded Value retained at:

```text
<device>/resource/temperature/state
```

The application does not need to assemble that MQTT topic.

## Observe another device

Declare a RemoteSensor:

```cpp
RemoteSensor<float> outsideTemperature("outside_temperature");
```

React to effective Value changes:

```cpp
void onOutsideTemperature(
    NetValue<float> &resource,
    const float &value)
{
    Serial.printf("Outside: %.2f\n", value);
}
```

Then bind it:

```cpp
outsideTemperature.onUpdate = onOutsideTemperature;

gResourcesManager.bindResource(&outsideTemperature);
outsideTemperature.setSource("weather-node", "temperature");
```

NightMare automatically subscribes to:

```text
weather-node/manifest
weather-node/resource/temperature/state
```

and restores those subscriptions after MQTT reconnect.

## Use a source selected at runtime

A Remote Resource can be bound before it has a source:

```cpp
RemoteSensor<float> selectedTemperature("selected_temperature");

void setup()
{
    gResourcesManager.bindResource(&selectedTemperature);
    startNightMareESP();
}
```

Later:

```cpp
selectedTemperature.setSource(
    "weather-node",
    "temperature");
```

The manager replaces the old subscriptions, resets state learned from the
previous source, saves the binding in `/remoteresources.json`, and republishes
the consume manifest. `startNightMareESP()` restores the saved binding on the
next boot after Resources are bound and before networking starts.

## Write another device's state

Declare:

```cpp
RemoteState<bool> bedroomPower("bedroom_power");
```

Bind it:

```cpp
gResourcesManager.bindResource(&bedroomPower);
bedroomPower.setSource("bedroom-ac", "power");
```

Request a change:

```cpp
if (!bedroomPower.setValue(true))
{
    Serial.println("write was not accepted for transport");
}
```

RemoteState is optimistic by default.

Immediately after a successfully transported write:

```cpp
bedroomPower.getValue();
```

shows the requested value for the optimistic window.

The owner's last reported truth remains available through:

```cpp
bedroomPower.authoritativeValue();
```

## Invoke a remote Action

Declare and bind:

```cpp
RemoteAction identifyRemote("bedroom_identify");

gResourcesManager.bindResource(&identifyRemote);
identifyRemote.setSource("bedroom-ac", "identify");
```

Invoke:

```cpp
bool sent = identifyRemote.invoke();
```

`true` means the request was accepted for transport.

It is not a remote execution acknowledgement.

Ordinary Resource `/invoke` is fire-and-forget.

## Add a Scheduler callback

For recurring runtime work:

```cpp
void sampleTemperature()
{
    // Read hardware.
    const float value = readTemperature();
    temperature.setValue(value);
}

void setup()
{
    // ...

    gScheduler.timer(
        "app.temperature",
        sampleTemperature,
        5000);

    startNightMareESP();
}
```

C++ callback jobs are MANAGED.

They cannot be removed by operator `JOB CLEAR`.

## Scheduler ownership order

A job may be added before `gScheduler.begin()` / `startNightMareESP()`.

This is useful for application setup:

```text
declare/bind Resources
register callbacks/jobs
start framework
```

Direct `gScheduler.tick()` is also allowed for manual/cooperative use.

In the standard lifecycle, use `tickNightMareESP()` instead of manually ticking a Scheduler configured in TASK mode.

## Operator commands

With Console built-ins enabled, the command grammar includes:

```text
PING
INFO ...
TIME
JOB ...
MQTT ...
WIFI ...
CONFIG ...
> ...
```

Commands can arrive through:

```text
serial console       when enabled
MQTT console
controlled MQTT / MQTTP
Scheduler String jobs
```

The `>` form routes command/control input to already-bound Resources. The character immediately after `>` is significant:

```text
>list
>raw <topic> [payload]

> temperature
> power set true
> identify invoke {"mode":"blink"}
```

No space after `>` selects a ResourceManager operation such as `list` or `raw`.

A space after `>` selects a bound Resource by its unique local name. A bare
Value reads its effective current Value; a bare Action invokes an empty
payload. Explicit Resource verbs are `get`, `set`, `invoke`, and `source`.

`>raw` feeds an MQTT-shaped topic/payload through the Resource ingress path. For example:

```text
>raw bedroom-ac/resource/power/set true
```

See [Commands](protocols/commands.md) and [Time](modules/time.md).

## What appears on MQTT

For a device named:

```text
living-room
```

the normal retained state may include:

```text
living-room/status
living-room/info
living-room/telemetry/system
living-room/telemetry/network
living-room/manifest
living-room/resource/temperature/state
living-room/resource/power/state
```

Requests arrive at:

```text
living-room/resource/power/set
living-room/resource/identify/invoke
living-room/console/in
living-room/console/controlled/<id>/in
```

See [MQTT topics](protocols/topics.md) for the complete wire map.

## Check the device identity

The generated default network name is based on the board hardware:

```cpp
Serial.println(gDeviceIdentity.getDeviceName());
```

Example:

```text
Esp32-nm-6ca172e0
```

The same generated name is also available permanently as:

```cpp
gDeviceIdentity.getHardwareSignature();
```

If the logical name is later adopted, the hardware signature remains unchanged.

The persisted POSIX timezone is available through:

```cpp
gDeviceIdentity.getTimezone();
```

Use `ADOPT <name>` or `CHANGE NAME <name>` for identity migration, and `TIMEZONE SET <posix-tz>` or `CHANGE TIMEZONE <posix-tz>` to change local-time presentation through the command surface.

## Next steps

Read these next:

- [Core concepts](concepts.md) for the vocabulary.
- [Resources](modules/resources.md) for the C++ Resource model.
- [Resource protocol](protocols/resources.md) for the wire contract.
- [Scheduler](modules/scheduler.md) for timing and persistence.
- [Device identity](modules/identity.md) for adoption and cleanup.
- [Network and MQTT](modules/network.md) for broker and callback behavior.
- [Known gaps](architecture/known-gaps.md) before depending on behavior that is intentionally deferred.
