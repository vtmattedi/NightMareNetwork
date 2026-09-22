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
    identity, hardware, physical connections, build, boot

SYSTEM
    changing runtime system health

NETWORK
    changing network bookkeeping
```

Only those three aggregate document types have MQTT topics.

## InfoType

```cpp
enum class InfoType : uint8_t
{
    INVALID,
    INFO,
    IDENTITY,
    HARDWARE,
    HW_CONNECTIONS,
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
HWCONNECTIONS
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
hwconnections
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

For example, `HWCONNECTIONS` returns the JSON array itself.

## INFO section contents

### Identity

```text
name
hardware
id
```

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

### Hardware connections

Each declared hardware connection includes:

```text
name
pin
direction
pull
active_low
note     optional
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
HWCONNECTIONS
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

attempts all three documents:

```text
INFO
SYSTEM
NETWORK
```

even if an earlier publication fails.

The return value is `true` only if all three publications succeed.

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

The slower cadence is deliberate because network telemetry is bookkeeping and is also refreshed on every MQTT connection.

## MQTT reconnect publication

Every MQTT connection calls:

```cpp
Telemetry.publishAll();
```

when telemetry is enabled.

This refreshes retained INFO, SYSTEM, and NETWORK documents after reconnect or broker switch.

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

## HardwareProfile dependency

Aggregate INFO and the `HWCONNECTIONS` query use:

```cpp
NMHardware::getProfile();
```

If no project `NightMareHardware.h` exists, the profile defaults to:

```text
board: unspecified
connections: none
```

The current telemetry implementation bounds hardware connections to 128 entries.

An unusable profile causes INFO/HWCONNECTIONS generation to fail rather than silently serialize invalid memory.
