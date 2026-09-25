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
```

INFO, SYSTEM, NETWORK, and hardware configuration have JSON MQTT topics.

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
    NETWORK
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

Current fields include:

```text
wifi_connected
ip
rssi_dbm
mqtt_connected
broker
```

`ip` and `rssi_dbm` are emitted only while WiFi is connected.

If WiFi support is disabled, WiFi-specific fields are omitted.

`broker` is:

```text
local
remote
```

## Publish one document

```cpp
Telemetry.publishInfo(InfoType::INFO);
Telemetry.publishInfo(InfoType::SYSTEM);
Telemetry.publishInfo(InfoType::NETWORK);
```

String forms are also available:

```cpp
Telemetry.publishInfo("SYSTEM");
```

Publication is retained and device-prefixed.

The topics are:

```text
<device>/info
<device>/telemetry/system
<device>/telemetry/network
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

attempts INFO, hardware configuration, and both telemetry documents:

```text
INFO
HARDWARE
SYSTEM
NETWORK
```

even if an earlier publication fails.

The return value is `true` only if all four publications succeed.

## Automatic startup

```cpp
Telemetry.start();
```

installs two Scheduler callback jobs:

```text
nm.telemetry.system
nm.telemetry.network
```

Both are MANAGED jobs.

The startup operation is all-or-nothing: if installing the network job fails after the system job succeeded, the system job is removed so a later retry does not collide with a half-installed telemetry configuration.

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

## MQTT reconnect publication

Every MQTT connection requests cooperative publication of:

```cpp
Telemetry.publishInfo(InfoType::INFO);
Telemetry.publishHardware();
```

when telemetry is enabled.

`tickNightMareESP()` processes one request per call, so the two
allocation-heavy static documents are not built during the MQTT/TLS connection
callback. SYSTEM and NETWORK continue on their periodic schedules.

## Why network telemetry can look stale after disconnect

Network telemetry is retained.

If a device disappears unexpectedly, the retained document may still contain:

```json
"mqtt_connected": true
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

The retained topic is:

```text
<device>/hardware
```

The readable JSON form is available through:

```cpp
Telemetry.getHardware();
```

Publication uses:

```cpp
Telemetry.publishHardware();
```

Version 2 stores assemblies, devices, connectors, contacts, terminals, reusable
definitions, and physical connections. It does not store normal nets. The
firmware validates the expanded configuration and infers connected components
with `buildTopologyGraph()` and `inferNets()`. Invalid or over-capacity
configuration fails publication rather than producing a partial retained
document. See [Hardware configuration v2](../hwconfig-v2-model.md).
