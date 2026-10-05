---
title: Telemetry and INFO
description: Query and publish device description and runtime telemetry.
section: modules
order: 40
---

# Telemetry and INFO

NightMare's telemetry service exposes device-wide information that is not application Resource state.

The public singleton is:

```cpp
Telemetry
```

The data is split by lifecycle:

```text
INFO
    identity, hardware, build, boot

HARDWARE CONFIGURATION
    assemblies, definitions, devices, connectors and physical connections

SYSTEM
    changing runtime system health

NETWORK
    changing network bookkeeping

HEARTBEAT
    transient liveness cadence configured at runtime
```

INFO, SYSTEM, NETWORK, and HEARTBEAT keep their existing JSON MQTT topics.
Hardware configuration is MessagePack-first; its JSON sibling is optional.

## InfoType

```cpp
enum class InfoType : uint8_t
{
    INVALID,
    INFO,
    IDENTITY,
    HARDWARE,
    BUILD,
    BOOT,
    SYSTEM,
    NETWORK,
    HEARTBEAT
};
```

The subsection types are useful for queries even though they do not each have a retained MQTT topic.

## String lookup

```cpp
InfoType getInfoType(const String &type);
```

Recognized names are case-insensitive:

```text
INFO
IDENTITY
HARDWARE
BUILD
BOOT
SYSTEM
NETWORK
HEARTBEAT
```

An empty String means:

```text
INFO
```

Unknown text maps to:

```cpp
InfoType::INVALID
```

## TelemetryResult

Queries return:

```cpp
struct TelemetryResult
{
    bool valid = false;
    String data;
};
```

`data` contains serialized JSON when valid.

## Query aggregate INFO

```cpp
TelemetryResult result =
    Telemetry.getInfo();
```

or:

```cpp
Telemetry.getInfo(InfoType::INFO);
```

The result contains:

```text
identity
hardware
build
boot
```

## Query one section

```cpp
Telemetry.getInfo(InfoType::SYSTEM);
```

or:

```cpp
Telemetry.getInfo("SYSTEM");
```

Section queries return the section value directly rather than wrapping it under its aggregate key.

## INFO section contents

### Identity

```text
name
hardware
id
timezone
```

`timezone` is the active persisted POSIX timezone. A successful timezone command refreshes the retained INFO document when MQTT is connected.

### Hardware

```text
board
chip
cores
revision
flash_bytes
heap_bytes
psram_bytes
```

### Build

```text
firmware_version
date
time
compiler
arduino_version
esp_idf_version
features
```

### Boot

Current boot information is intentionally minimal:

```text
reset_reason
```

Changing facts do not belong in the boot/static aggregate.

## SYSTEM contents

Current fields:

```text
uptime_ms
cpu_mhz
free_heap_bytes
min_free_heap_bytes
free_psram_bytes
```

## NETWORK contents

The document has five independent objects:

```text
transport.preferred / active / state
wifi_radio.supported / enabled / state
wifi_ip.supported / enabled / state
esp_now.supported / enabled / state
mqtt.supported / enabled / state / profile
```

Connected WiFiIP additionally reports `ssid`, `ip`, `rssi`, `channel`, and
`tx_power_dbm`. ESP-NOW reports already-available gateway/session facts without
starting discovery. Unsupported services remain explicit with
`supported=false`.

Readable transport names are:

```text
AUTO
MQTT
ESP_NOW
```

Preference and active transport are separate so a disabled/unavailable
preferred service can coexist with a connected fallback.

`gateway_candidate` reports already-known retained gateway readiness: `known`,
`id`, `probable`, `esp_now_ready`, `remote_mqtt_ready`, `local_mqtt_ready`,
`ssid`, `bssid`, and `channel`. Producing telemetry does not scan or discover.

## HEARTBEAT contents

```json
{
  "uptime_ms": 123456,
  "heartbeat": 42
}
```

The counter advances only after MQTT accepts a heartbeat publication. The
heartbeat is transient and does not replace retained `/status` plus Last Will
as the authoritative presence mechanism.

## Publish one document

```cpp
Telemetry.publishInfo(InfoType::INFO);
Telemetry.publishInfo(InfoType::SYSTEM);
Telemetry.publishInfo(InfoType::NETWORK);
Telemetry.publishInfo(InfoType::HEARTBEAT);
```

String forms are also available:

```cpp
Telemetry.publishInfo("SYSTEM");
```

Publication is device-prefixed. INFO, SYSTEM, and NETWORK are retained;
HEARTBEAT is not retained.

The topics are:

```text
<device>/info
<device>/telemetry/system
<device>/telemetry/network
<device>/telemetry/heartbeat
```

## Query-only sections cannot be published independently

These are queryable:

```text
IDENTITY
HARDWARE
BUILD
BOOT
```

but do not have individual MQTT topics.

Therefore:

```cpp
Telemetry.publishInfo(InfoType::HARDWARE);
```

returns `false`.

## Publish all documents

```cpp
Telemetry.publishAll();
```

attempts INFO, hardware configuration, system, network, and heartbeat when it
is enabled:

```text
INFO
HARDWARE
SYSTEM
NETWORK
HEARTBEAT
```

even if an earlier publication fails.

The return value is `true` only if all enabled publications succeed.

## Automatic startup

```cpp
Telemetry.start();
```

installs the system and network Scheduler callback jobs plus heartbeat when it
is enabled:

```text
nm.telemetry.system
nm.telemetry.network
nm.telemetry.heartbeat
```

All installed telemetry jobs are MANAGED jobs.

The startup operation is all-or-nothing. Any failure removes jobs installed by
that attempt so a later retry does not collide with a half-installed telemetry
configuration.

## Heartbeat configuration

Heartbeat is enabled by default with a 15-second period. It is controlled by
two persistent Configs:

```text
heartbeat:enable    boolean, default true
heartbeat:period    integer seconds, default 15, range 15..86400
```

For example:

```text
CONFIG SET heartbeat:enable false
CONFIG SET heartbeat:period 60
CONFIG SET heartbeat:enable true
```

The Configs use typed per-Config `onWrite` callbacks. Changing the period while
enabled immediately replaces the MANAGED heartbeat job. Disabling removes the
job; enabling installs it with the current period. An out-of-range period is
rejected without changing the Config or current schedule.

## System interval

System telemetry uses:

```cpp
NM_TELEMETRY_INTERVAL_MS
```

Default:

```text
60000 ms
```

The job is recurring monotonic runtime work.

It does not persist across reboot.

## Network interval

Network telemetry uses:

```cpp
NM_NETWORK_TELEMETRY_INTERVAL_MS
```

Default:

```text
300000 ms
```

The slower cadence is deliberate because network telemetry is bookkeeping.

## Active-transport reconnect and state-change publication

Every MQTT connection requests cooperative publication of:

```cpp
Telemetry.publishInfo(InfoType::INFO);
Telemetry.publishHardware();
```

when telemetry is enabled.

`tickNightMareESP()` processes one request per call, so the two
allocation-heavy static documents are not built during the MQTT/TLS connection
callback. Meaningful connectivity-service, preferred-transport, and
active-transport changes request a coalesced NETWORK refresh. SYSTEM and
NETWORK also continue on their periodic schedules. HEARTBEAT continues on its
runtime-configured schedule when enabled.

## Why network telemetry can look stale after disconnect

Network telemetry is retained.

If a device disappears unexpectedly, the retained document may still contain:

```json
"mqtt": {"state": "CONNECTED"}
```

because the device is no longer available to rewrite it.

This is expected.

Presence belongs to:

```text
<device>/status
```

Network telemetry is last-known bookkeeping.

## Resource state is not telemetry

NightMare deliberately does not copy application Values into a generic telemetry blob.

For example:

```text
temperature
power
target_temperature
door_open
```

belong to Resources.

Resources already define:

- ownership,
- type,
- access,
- retained state,
- freshness,
- subscriptions.

Duplicating those fields into telemetry would create a second source of application truth.

## Hardware configuration

Hardware configuration uses:

```cpp
NMHardware::getProfile();
```

If no project `NightMareHardware.h` exists, the profile defaults to:

```text
host_assembly: main
roots: [{ id: main, kind: generic }]
definitions: none
connections: none
```

The canonical retained topic is:

```text
<device>/hardware/msgpack
```

The readable JSON form is available on demand through:

```cpp
Telemetry.getHardware();
```

Publication uses:

```cpp
Telemetry.publishHardware();
```

It publishes MessagePack canonically and, only when `NM_ENABLE_JSON_WIRE=1`,
also publishes `<device>/hardware/json`. Failure of that optional sibling does
not make canonical publication fail. The bare hardware root carries no payload.

Version 2 stores assemblies, devices, connectors, contacts, terminals, reusable
definitions, and physical connections. It does not store normal nets. The
firmware validates the expanded configuration and infers connected components
with `buildTopologyGraph()` and `inferNets()`. Invalid or over-capacity
configuration fails publication rather than producing a partial retained
document. See [Hardware configuration v2](../hwconfig-v2-model.md).
