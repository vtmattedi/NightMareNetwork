---
title: ESP32 platform lifecycle
description: Feature flags, hardware profile, WiFi, time, OTA, and NightMareESP lifecycle helpers.
section: modules
order: 70
---

# ESP32 platform lifecycle

The active NightMare implementation targets:

```text
Arduino
ESP32 / espressif32
```

The core abstractions are kept reasonably separate, but several platform services still use ESP32, Arduino, FreeRTOS, LittleFS, and `esp_mqtt_client` directly.

The main project-facing lifecycle is:

```cpp
startNightMareESP();
tickNightMareESP();
```

## Main include

Most applications can include:

```cpp
#include <NightMare.h>
```

This exposes the enabled NightMare modules according to compile-time feature configuration.

## NightMareConfig.h

A consuming project may provide:

```text
include/NightMareConfig.h
```

`Features.h` includes it automatically when present.

The project can define feature macros before NightMare's defaults are applied.

Example:

```cpp
#pragma once

#define NM_FIRMWARE_VERSION "1.2.0"

#define NM_ENABLE_OTA 1
#define NM_ENABLE_HTTP 0
#define NM_ENABLE_WEBSOCKET 0

#define NM_SCHEDULER_OWN_TASK 1

#define NM_TIMEZONE "UTC0"
#define NM_NTP_SERVER_1 "pool.ntp.org"
#define NM_NTP_SERVER_2 "time.nist.gov"
#define NM_NTP_SERVER_3 "time.google.com"

#define NM_TELEMETRY_INTERVAL_MS 60000UL
#define NM_NETWORK_TELEMETRY_INTERVAL_MS 300000UL
```

## Default feature flags

Current defaults are:

```text
NM_ENABLE_SETTINGS             1
NM_ENABLE_RESOURCES            1
NM_ENABLE_NETWORK              1
NM_ENABLE_CONSOLE              1
NM_ENABLE_WIFI                 1
NM_ENABLE_MQTT                 1
NM_ENABLE_TELEMETRY            1
NM_ENABLE_SCHEDULER            1
NM_ENABLE_JOBS                 1
NM_ENABLE_TIME_SYNC            1

NM_ENABLE_OTA                  0
NM_ENABLE_HTTP                 0
NM_ENABLE_WEBSOCKET            0
NM_ENABLE_LVGL                 0

NM_ENABLE_ACTION_PAYLOAD_ASSERTION  0

NM_CONSOLE_BUILTINS            1
NM_CONSOLE_SERIAL              0
```

The platform marker is currently:

```text
NM_PLATFORM_ESP32 1
```

and is reserved for a future platform split.

## Time defaults

When time synchronization is enabled, the current defaults are:

```text
NM_TIMEZONE      "UTC0"
NM_NTP_SERVER_1  "pool.ntp.org"
NM_NTP_SERVER_2  "time.nist.gov"
NM_NTP_SERVER_3  "time.google.com"
```

`NM_TIMEZONE` is the default POSIX timezone string. `DeviceIdentity` persists runtime changes under its private settings and applies the active value for local-time formatting. Epoch timestamps remain UTC-based.

## Important configurable intervals

Defaults:

```text
NM_TELEMETRY_INTERVAL_MS           60000
NM_NETWORK_TELEMETRY_INTERVAL_MS  300000
NM_IDENTITY_CLEANUP_RETRY_MS       60000
```

## Scheduler execution mode

```text
NM_SCHEDULER_OWN_TASK 1
    startNightMareESP() selects Scheduler TASK mode

NM_SCHEDULER_OWN_TASK 0
    startNightMareESP() selects Scheduler MANUAL mode
```

In MANUAL mode:

```cpp
tickNightMareESP();
```

drives the Scheduler.

## Compile-time dependencies

The current feature graph enforces:

```text
MQTT
    -> NETWORK

NETWORK
    -> RESOURCES

TELEMETRY
    -> SCHEDULER
    -> MQTT

JOBS
    -> SCHEDULER

SCHEDULER
    -> SETTINGS
    -> CONSOLE

CONSOLE
    -> SETTINGS

TIME_SYNC
    -> WIFI
    -> SETTINGS

WIFI
    -> SETTINGS

OTA
    -> WIFI
    -> SETTINGS

HTTP
    -> NETWORK
    -> CONSOLE

WEBSOCKET
    -> HTTP

CONSOLE_SERIAL
    -> CONSOLE
```

These are current implementation dependencies, not necessarily permanent architectural requirements.

## HardwareProfile

Projects can optionally provide:

```text
NightMareHardware.h
```

That file should expose the project's hardware profile through:

```cpp
NMHardware::Profile projectProfile();
```

The model is:

```cpp
struct Board
{
    const char *id;
    const char *model;
};

enum class DeviceKind
{
    Unknown, Ic, Led, Button, Relay, Sensor, Display, Speaker,
    Buzzer, Connector, Transistor, Diode, Resistor, Capacitor,
    Motor, Storage
};

struct Device
{
    const char *id;
    const char *model;
    uint8_t board; // board index, or NoBoard for an external discrete part
    DeviceKind kind; // defaults to Unknown
    const char *form; // defaults to nullptr
};

enum class EndpointKind { Board, Device, External };

struct Endpoint
{
    EndpointKind kind;
    uint8_t index;
    const char *terminal;
};

struct Net
{
    const char *id;
    SignalType type;
    uint8_t bus;
    Direction direction;
    Pull pull;
    bool activeLow;
    Resistor resistor;
};

struct Connection
{
    Endpoint from;
    Endpoint to;
    uint8_t net;
    uint8_t group;
};

struct Profile
{
    uint8_t hostBoard;
    const Board *boards;
    size_t boardCount;
    const Device *devices;
    size_t deviceCount;
    const Net *nets;
    size_t netCount;
    const Connection *connections;
    size_t connectionCount;
};
```

Directions:

```text
Input
Output
Bidirectional
Power
Ground
Bus
```

Pulls:

```text
None
Up
Down
ExternalUp
ExternalDown
```

If no `NightMareHardware.h` exists, NightMare returns:

```text
boards:      [{ id: main, model: unspecified }]
devices:     none
nets:        none
connections: none
```

The model of `hostBoard` also feeds INFO/HARDWARE. The complete topology is
published at `<device>/hardware` and `<device>/hardware/msgpack` and is
available through the `HW` command.

A physical PCB/module should normally be a Board; a chip/component mounted on
it is a Device. `NoBoard` is reserved for genuinely external discrete parts.
Connections are explicit endpoint-to-endpoint physical segments. Nets carry
electrical meaning, while non-zero connection groups describe bundled wires.
Optional Device `kind` and `form` values improve visualization without being
required. `kind` is a broad category such as `Sensor` or `Led`; `form` is a
stable lowercase physical-form slug and is not a UI artwork identifier.

## `startNightMareESP()`

The standard startup helper performs common framework setup in this order.

### 1. Device identity

```cpp
gDeviceIdentity.begin();
```

### 2. Scheduler

When enabled:

```cpp
gScheduler.begin(
    NM_SCHEDULER_OWN_TASK
        ? SchedulerRunMode::TASK
        : SchedulerRunMode::MANUAL);
```

### 3. Pending identity cleanup retry

When Scheduler + MQTT are enabled and cleanup is pending, NightMare installs:

```text
_nm_identity_cleanup
```

as a recurring MANAGED callback job.

### 4. Telemetry

When enabled:

```cpp
Telemetry.start();
```

installs the periodic system/network publication jobs.

### 5. WiFi

When enabled:

```cpp
WiFi_Auto();
```

starts the asynchronous WiFi path.

## Resource registration comes first

Application Resources should be bound before:

```cpp
startNightMareESP();
```

Example:

```cpp
void setup()
{
    power.onWrite = onPowerWrite;

    gResourcesManager.bindResource(&power);
    gResourcesManager.bindResource(&temperature);

    startNightMareESP();

    startApplicationHardware();
}
```

This ensures Resource declarations exist before MQTT participation and before old-identity cleanup may run.

## `tickNightMareESP()`

The framework cooperative tick currently handles:

```text
Time synchronization completion
    when NM_ENABLE_TIME_SYNC

Scheduler
    only when Scheduler mode is MANUAL

Serial console
    only when NM_ENABLE_CONSOLE && NM_CONSOLE_SERIAL
```

SNTP completion is intentionally dispatched here because the ESP callback runs on lwIP's task. NightMare defers `RuntimeState` bookkeeping and the application `onTimeSync()` callback to the normal cooperative context.

Task-driven/event-driven subsystems such as MQTT and WiFi are not otherwise polled here.

## WiFi project credentials

When WiFi is enabled, the current platform code includes a consuming-project header:

```text
creds.h
```

and requires:

```cpp
DEFAULT_SSID
DEFAULT_PASSWORD
```

The project, not the NightMare library, owns this credential file.

## WiFi_Auto

```cpp
WiFi_Auto();
```

initializes PersistentSettings.

If either stored key is absent:

```text
_ssid
_password
```

the current implementation writes defaults from:

```text
DEFAULT_SSID
DEFAULT_PASSWORD
```

Then it starts an asynchronous connection using the stored values.

## WiFi hostname and identity

Both synchronous and asynchronous WiFi connection paths set the hostname from:

```cpp
gDeviceIdentity.getDeviceName()
```

and lock the identity address before network participation.

This is one reason a later adoption may wait for reboot rather than changing the running name immediately.

## Async WiFi task

`WiFi_ConnectAsync()` starts:

```text
WiFi_Task
```

with:

```text
stack:     4096 bytes
priority:  1
core:      tskNO_AFFINITY
```

Using no fixed core allows the same code to run on single-core ESP variants such as C3/C6/H2/S2.

## First WiFi connection

On the first successful WiFi connection in a boot, the current implementation starts enabled network services:

```text
OTA
MQTT
SNTP time synchronization
```

in that order.

Specifically:

```cpp
initOTA();
MQTT_Init(false);
startSntpTimeSync();
```

according to feature flags.

`MQTT_Init(false)` selects Remote MQTT initially.

`startSntpTimeSync()` configures the ESP32 SNTP client and returns immediately. Completion is applied later through `tickNightMareESP()`.

## Later WiFi reconnects

The WiFi layer tracks whether this is its first successful connection.

The first-connection initialization block is not rerun on every later WiFi reconnect.

Long-lived network services are expected to manage their own connection/reconnect lifecycle once started.

## WiFi callback

Projects can register:

```cpp
WiFi_onConnected(callback);
```

with signature:

```cpp
void callback(bool firstConnection);
```

The callback runs whenever the WiFi monitor detects a connection and tells the project whether this is the first successful connection handled by that module.

## WiFi credential change

```cpp
WiFi_ChangeCredentials(ssid, password);
```

does the following:

1. disconnects current WiFi,
2. tries the new credentials synchronously for up to 15 seconds,
3. if they fail, starts reconnect with the previous stored credentials,
4. if they succeed, writes `_ssid` and `_password` to PersistentSettings.

The private storage keys should not be manipulated directly by applications.

## MQTT platform transport

The ESP MQTT implementation uses:

```text
esp_mqtt_client
FreeRTOS control queue/task
```

Local broker:

```text
mqtt://...
```

Remote broker:

```text
mqtts://...
```

The remote broker is verified with the project-provided:

```text
ROOT_CA
```

See [Network and MQTT](network.md).

## Time synchronization

Automatic time synchronization now uses the ESP32 SNTP client rather than a blocking HTTP request.

The canonical API is:

```cpp
startSntpTimeSync();
```

It uses the process `TZ` value applied by `DeviceIdentity`, plus `NM_NTP_SERVER_1..3`.

Synchronization is asynchronous. The SNTP callback marks a pending event; `tickNightMareESP()` later calls `processTimeSyncEvents()` so `SystemState` and application callbacks are not touched from lwIP's task.

See [Time](time.md) for the complete clock, timezone, formatting, and synchronization API.

## MQTT-assisted time sync

When MQTT connects and NightMare wall time is invalid, it can also publish:

```text
Control/request
```

with payload:

```text
time
```

and consume a `Control/time` JSON response.

That path ultimately calls:

```cpp
manualSyncTime(timestamp);
```

## Time-sync runtime state

Successful time sync updates:

```text
SystemState["time_synced"] = "1"
SystemState["boot_time"]   = <derived epoch>
```

and invokes the optional callback installed by:

```cpp
onTimeSync(callback);
```

## OTA

OTA is disabled by default:

```text
NM_ENABLE_OTA 0
```

When enabled, it depends on WiFi + Settings and is initialized on the first successful WiFi connection.

OTA is a platform service, not part of the Resource protocol.

## HTTP, WebSocket, and LVGL

These modules remain optional:

```text
NM_ENABLE_HTTP
NM_ENABLE_WEBSOCKET
NM_ENABLE_LVGL
```

They are not part of the core Resource architecture and have not received the same freeze-level architecture pass.

New core design should not use those modules as precedent for Resource/network semantics.

## Platform boundary today

The current library already separates several concepts cleanly:

```text
Resource model
DeviceIdentity state
Scheduler model
Telemetry model
MQTT facade vs ESP transport
```

but the complete library is not yet portable beyond ESP32.

Current platform-specific dependencies include:

```text
ESP hardware APIs
FreeRTOS
LittleFS
Arduino WiFi
esp_mqtt_client
ArduinoOTA
esp_sntp
```

A future platform split can move those implementations without requiring the Resource protocol itself to become platform-specific.
