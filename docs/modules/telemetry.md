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

HARDWARE TOPOLOGY
    board, devices, buses/signals, pins and electrical attributes

SYSTEM
    changing runtime system health

NETWORK
    changing network bookkeeping
```

INFO, SYSTEM and NETWORK have JSON MQTT topics. Hardware topology has both JSON
and MessagePack retained topics.

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

attempts the INFO, both hardware, and both telemetry documents:

```text
INFO
HARDWARE JSON
HARDWARE MSGPACK
SYSTEM
NETWORK
```

even if an earlier publication fails.

The return value is `true` only if all five publications succeed.

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

## Hardware topology

Hardware topology uses:

```cpp
NMHardware::getProfile();
```

If no project `NightMareHardware.h` exists, the profile defaults to:

```text
boards: [{ id: main, model: unspecified }]
connections: none
```

The retained topics are:

```text
<device>/hardware
<device>/hardware/msgpack
```

The readable form is available through:

```cpp
Telemetry.getHardware(HardwareFormat::JSON);
```

Publication methods are:

```cpp
Telemetry.publishHardware(HardwareFormat::JSON);
Telemetry.publishHardware(HardwareFormat::MSGPACK);
Telemetry.publishHardware(); // both
```

The version 2 compact schema is
`[version, boards[], devices[], connections[]]`. Boards are `[id, model]`, and
devices are `[id, model, boardIndex]`. `boards[0]` is the main board. Connections
use positional pin, device index, signal, bus index, numeric signal type,
numeric direction, numeric pull, active-low, and an optional resistor.
`boardIndex` is `255` for a standalone component; readable JSON represents that
value as `null`.

A `Resistor` occupies two bytes: BCD digits `a,b` and signed exponent `c` for
`a.b × 10^c` ohms. Constructors accept numeric ohms and strings such as
`"3k3"`, `"4.7k"`, `"4M7"`, and `"0.33"`.

Pull modes are `None`, `Up`, `Down`, `ExternalUp`, and `ExternalDown`.
Rendering coordinates, SVG, icons, footprints, and artwork remain server-side.

The implementation bounds hardware connections to 128 entries and boards and
devices to 255 each. A profile must contain at least one board, and every device
must reference a valid board index or `NoBoard`. An unusable profile causes
topology generation to fail rather than silently serialize invalid memory.
