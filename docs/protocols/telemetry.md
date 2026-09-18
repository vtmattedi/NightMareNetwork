---
title: Telemetry and identity
description: The status, telemetry and descriptor messages every device publishes, and the three info commands every device answers.
section: protocols
order: 60
---

# Telemetry and identity

The library publishes a device's presence and health without the device doing
anything. This page is the shape of those messages, and of the three commands
that answer the same questions on demand.

## `status` — presence

```
<Device>/status   online     retained
<Device>/status   offline    retained, set by the broker as the last-will
```

Published `online` on every connect, which also overwrites a stale retained
`offline`. Consumers treat a device with no retained status as offline, and an
empty retained payload as a deletion.

## `telemetry` — health

Every **15 s**, from a timer the library creates in `TimersHandler`'s
constructor. The payload is `getSystemStatus()`:

```json
{
  "System": {
    "Uptime": 3612,
    "FreeHeap": 18.2,
    "boot_time": "2026-09-18 08:41:03",
    "time_synced": true,
    "reset_reason": 1,
    "wifi_rssi": -61,
    "mqtt_connection": "Remote",
    "ip_address": "10.10.3.11",
    "direct_http": false,
    "OTA_enabled": true,
    "ASYNC_enabled": true
  }
}
```

The top-level `System` key is load-bearing: the backend does
`JSON.parse(payload).System` and stores that as the device's info. Modules that
are not compiled in report a disabled value rather than vanishing, so the
document keeps its shape across builds. `FreeHeap` is a percentage used.

Devices should not add their own telemetry timer — several did, and published
twice. If 15 s is too chatty for a cloud broker, the interval is changed in the
library, once.

## `info` — the descriptor

Published on connect and on `INFO`. It is the answer to "what is this device
and is it working", in one message:

```json
{
  "device": "Adler",
  "firmware": "1.1.130",
  "board": {
    "name": "ESP32-C3 SuperMini rev1", "revision": "BOARD_C3_V1",
    "connections": [ { "gpio": 7, "device": "IR RX demodulator", "level": 1, "notes": "idles HIGH" } ]
  },
  "sensors":     { "temperature": { "id": "temperature", "label": "Room temperature", "unit": "°C", "...": "..." } },
  "actuators":   { "ir": { "hardware": "IR LED + TSOP", "pins": { "tx": 8, "rx": 7 }, "codes": ["POWER", "PLUS"] } },
  "controllers": { "ac": { "service": "AcController", "config": { "ac_hysteresis": 0.5 } } }
}
```

`sensors` is the declaration from [Sensors](/docs/protocols/sensors), with a
live `value` and `age_ms` added. `board` comes from the device's board
registry, with each pin's level as read right now — which is how a unit can be
checked without opening the case.

## The three info commands

Every device answers these, because the library does. They are what the
Dashboard's device page offers, and what the backend reads on discovery.

| command | answers with |
| --- | --- |
| `BOOTINFO` | `{"ResetReason", "IsTimeSynced", "CurrentTime", "Uptime", "BootTime"}` |
| `HARDWAREINFO` | `{"ChipModel", "ChipCores", "ChipRevision", "FlashSizeMB", "HeapSize", "PsramSize", "MACAddress"}` |
| `SYSTEMINFO` | the `telemetry` document |

`HARDWAREINFO` is chip-level. `INFO` is the device-level complement — what is
*wired to* the chip.

## Reset reasons

`reset_reason` is the ESP-IDF `esp_reset_reason()` code. The ones worth knowing
on sight:

| code | meaning |
| --- | --- |
| 1 | power-on |
| 3 | software reset (`REBOOT`, OTA) |
| 4 | watchdog — a task held the CPU; look for a blocking read on the loop task |
| 15 | brown-out — the supply sagged; a USB port or a long thin wire |
| 12 | software CPU reset (panic) |
