---
title: Status, info, and telemetry
description: Presence, boot-scoped device description, and runtime telemetry documents.
section: protocols
order: 30
---

# Status, info, and telemetry

NightMare separates device-level information according to lifecycle:

```text
/status
    presence

/info
    boot-scoped / effectively static description

/hardware, /hardware/msgpack
    hardware-only topology in readable and compact forms

/telemetry/system
    changing runtime system health

/telemetry/network
    changing network bookkeeping
```

Application sensor and actuator state is not duplicated here. It belongs to Resources.

## Status

Topic:

```text
<device>/status
```

Status is retained.

Its JSON shape is always:

```json
{
  "name": "bedroom-ac",
  "hardware": "Esp32-nm-6ca172e0",
  "timezone": "<-03>3",
  "online": true
}
```

Fields:

```text
name
    logical/adoptable device name represented by this status topic

hardware
    stable hardware signature of the physical board

timezone
    persisted POSIX timezone used for local-time presentation

online
    current presence state represented by this publication
```

The same JSON shape is used for:

- normal online publication,
- graceful offline publication,
- MQTT Last Will,
- cleanup of a previous adopted identity.

Observers therefore do not need separate status parsers for graceful and ungraceful disconnects.

## Status and Last Will

The MQTT client installs an offline Last Will for the current device's status topic.

When the connection becomes active, NightMare publishes the same document with:

```json
"online": true
```

On graceful stop it publishes:

```json
"online": false
```

before ending the client.

The status document is the presence signal. It does not assert freshness of every Resource.

## Hardware signature

The `hardware` field is not the logical MQTT identity.

A device may be adopted from:

```text
Esp32-nm-6ca172e0
```

to:

```text
bedroom-ac
```

while status still reports:

```json
{
  "name": "bedroom-ac",
  "hardware": "Esp32-nm-6ca172e0",
  "timezone": "<-03>3",
  "online": true
}
```

This allows network identity and physical-board identity to remain distinguishable.

## Info

Topic:

```text
<device>/info
```

`/info` is retained.

It is an aggregate of facts that are static or boot-scoped enough that they do not need periodic refresh during one boot:

```text
identity
hardware
build
boot
```

A representative structure is:

```json
{
  "identity": {
    "name": "bedroom-ac",
    "hardware": "Esp32-nm-6ca172e0",
    "id": "0011223344556677",
    "timezone": "<-03>3"
  },
  "hardware": {
    "board": "ESP32-C3 SuperMini rev1",
    "chip": "ESP32-C3",
    "cores": 1,
    "revision": 4,
    "flash_bytes": 4194304,
    "heap_bytes": 327680,
    "psram_bytes": 0
  },
  "build": {},
  "boot": {
    "reset_reason": 1
  }
}
```

Exact values depend on the board and build.

## Identity section

`identity` contains:

```text
name
hardware
id
timezone
```

`name` is the current logical identity.

`hardware` is the stable hardware signature.

`id` is the raw machine-oriented device ID.

`timezone` is the persisted POSIX `TZ` string. Changing it refreshes `/status` and `/info` while MQTT is connected.

## Hardware section

`hardware` contains:

```text
board
chip
cores
revision
flash_bytes
heap_bytes
psram_bytes
```

`board` comes from the active NightMare hardware profile.

The remaining hardware fields come from the ESP runtime.

## Hardware topology

Topology is not embedded in `/info`. It is retained separately as readable
JSON at `<device>/hardware` and compact MessagePack at
`<device>/hardware/msgpack`.

The MessagePack schema is versioned and positional:

```text
[
  version,
  boardId,
  devices[],
  connections[]
]

device     = [id, model]
connection = [pin, deviceIndex, signal, busIndex,
              signalTypeEnum, directionEnum, pullEnum, activeLow,
              resistor?]
```

`255` means no device or no bus. Numeric enums are append-only:

```text
direction:  0 input, 1 output, 2 bidirectional, 3 power, 4 ground, 5 bus
pull:       0 none, 1 up, 2 down, 3 external-up, 4 external-down
signal:     0 gpio, 1 SPI clock, 2 SPI MOSI, 3 SPI MISO, 4 SPI chip-select,
            5 I2C data, 6 I2C clock, 7 UART transmit, 8 UART receive,
            9 PWM, 10 analog, 11 one-wire, 12 power, 13 ground
```

The optional resistor is a two-byte value. Its first byte contains two BCD
digits `a` and `b`; its second byte is signed exponent `c`, representing
`a.b × 10^c` ohms. Thus 330 Ω is `(3,3,2)`, 3k3 is `(3,3,3)`, and
0.33 Ω is `(3,3,-1)`.

The JSON document uses named keys and enum names while describing the same
topology. Footprint coordinates, SVG artwork, icons, and rendering metadata are
not device payload data and remain server-side.

## Build section

`build` contains:

```text
firmware_version
date
time
compiler
arduino_version
esp_idf_version
features
```

`firmware_version` comes from:

```cpp
NM_FIRMWARE_VERSION
```

The `features` object reports the compile-time state of:

```text
settings
network
wifi
mqtt
console
resources
scheduler
jobs
telemetry
time_sync
ota
http
websocket
lvgl
```

Example:

```json
{
  "firmware_version": "1.1.141",
  "date": "Sep 21 2026",
  "time": "21:17:38",
  "compiler": "...",
  "arduino_version": 10819,
  "esp_idf_version": "...",
  "features": {
    "settings": true,
    "network": true,
    "wifi": true,
    "mqtt": true,
    "console": true,
    "resources": true,
    "scheduler": true,
    "jobs": true,
    "telemetry": true,
    "time_sync": true,
    "ota": false,
    "http": false,
    "websocket": false,
    "lvgl": false
  }
}
```

## Boot section

The current boot document contains:

```text
reset_reason
```

`reset_reason` is the numeric `esp_reset_reason()` value.

Changing information such as uptime is intentionally not placed in `/info`.

## System telemetry

Topic:

```text
<device>/telemetry/system
```

The document is retained.

Current fields are:

```json
{
  "uptime_ms": 123456,
  "cpu_mhz": 160,
  "free_heap_bytes": 210000,
  "min_free_heap_bytes": 198000,
  "free_psram_bytes": 0
}
```

Fields:

```text
uptime_ms
cpu_mhz
free_heap_bytes
min_free_heap_bytes
free_psram_bytes
```

System telemetry is normally refreshed every:

```text
NM_TELEMETRY_INTERVAL_MS
```

whose default is:

```text
60000 ms
```

## Network telemetry

Topic:

```text
<device>/telemetry/network
```

The document is retained.

When WiFi is enabled and connected, the document contains:

```json
{
  "wifi_connected": true,
  "ip": "192.168.1.50",
  "rssi_dbm": -55,
  "mqtt_connected": true,
  "broker": "remote"
}
```

When WiFi is enabled but disconnected:

```json
{
  "wifi_connected": false,
  "mqtt_connected": false,
  "broker": "remote"
}
```

`ip` and `rssi_dbm` are only present while WiFi reports connected.

If the library is built without WiFi support, the WiFi-specific fields are not emitted.

`broker` is one of:

```text
local
remote
```

Network telemetry is normally refreshed every:

```text
NM_NETWORK_TELEMETRY_INTERVAL_MS
```

whose default is:

```text
300000 ms
```

## Last-known network bookkeeping

Network telemetry is retained, but it is not the authoritative presence signal.

If a device disappears unexpectedly, its last retained network telemetry may still say:

```json
"mqtt_connected": true
```

because the device cannot rewrite telemetry after it has disappeared.

Use:

```text
<device>/status
```

for presence.

Use network telemetry as last-known bookkeeping.

## Publication on MQTT connection

Every MQTT connection or broker switch refreshes the five retained information documents:

```text
<device>/info
<device>/hardware
<device>/hardware/msgpack
<device>/telemetry/system
<device>/telemetry/network
```

This happens through `Telemetry.publishAll()`.

Each document is attempted even if publication of another document fails.

Network telemetry therefore identifies the broker actually active at the latest successful refresh.

## Periodic publication

`Telemetry.start()` installs two MANAGED monotonic callback jobs:

```text
nm.telemetry.system
nm.telemetry.network
```

They publish at the configured system and network intervals.

These are framework jobs, not USER jobs, so operator `JOB DELETE` and `JOB CLEAR` cannot remove them.

## INFO query interface

The same data can be queried without MQTT publication.

Supported sections are:

```text
INFO
IDENTITY
HARDWARE
BUILD
BOOT
SYSTEM
NETWORK
```

`INFO` means the aggregate retained `/info` shape.

The others return only the requested section.

For example:

```text
INFO HARDWARE
```

returns an object containing only hardware fields.

Hardware connections use the separate command:

```text
HW [PUBLISH] [JSON|MSGPACK]
```

JSON can be returned directly. A MessagePack request republishes the retained
MQTT document and answers `Republished to MQTT.`

## Publishable documents

Only three `InfoType` values map to retained MQTT documents:

```text
INFO
SYSTEM
NETWORK
```

Therefore:

```text
INFO PUBLISH
INFO PUBLISH SYSTEM
INFO PUBLISH NETWORK
```

are valid publication requests.

Section-only types such as `HARDWARE` or `BOOT` are queryable but are not independent MQTT documents.

## No heartbeat

NightMare currently defines no separate heartbeat topic.

Presence is represented by status + MQTT Last Will.

Application state freshness is represented by Resource `/state`.

A heartbeat can be added later if a concrete requirement is not met by those two mechanisms.
