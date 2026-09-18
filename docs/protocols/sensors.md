---
title: Sensors
description: How a device publishes readings, declares what its sensors are, and reads another device's sensor as if it were its own.
section: protocols
order: 40
---

# Sensors

A sensor has two things to say: **data** — the readings, with age, `null` when
unknown — and **info** — what it is. Readings go out continuously on a topic;
the declaration is answered on request. The backend keeps both, forever.

## Readings: `<Device>/sensors`

One JSON object, one field per sensor key, scalar values:

```json
{"temperature": 23.44, "door": false, "light": true}
```

Published on any change and on a heartbeat (60 s is the convention). Not
retained.

Consumers expand the object into one entry per field; a nested object joins
keys with `/`, so `{"dht": {"temp": 21.5}}` is the sensor `dht/temp`. A bare
scalar on `sensors/<key>` is accepted too, but the object is one message
instead of many and can say a sensor is *absent*.

### Rules the backend's storage imposes

These are ingest rules in the backend, and they decide how a value looks in the
database for good:

- `""`, `null`, `nan`, `undefined` are **not stored**. A sensor with nothing to
  say publishes `null`; its history gets a gap, not a zero. `ArduinoJson`
  serialises `NAN` as `null`, so a `float` initialised to `NAN` does this for
  free.
- `true`/`false` are stored as `1`/`0`. Publish booleans as booleans.
- **Type sticks to the first reading.** A temperature that first arrives as
  `23` is an `int` for the life of the row. Publish floats with a decimal
  point always — a `float` field in `ArduinoJson` does.
- Unit and label come from the declaration below. With none, the backend
  guesses `°C` from a key containing `temp` and `%` from `humid`, and names the
  sensor `<Device>/sensors/<key>`.

## The declaration: `sensors`

The backend asks every device `sensors` — the bare word, no subcommand — on
discovery and again each time the device comes back online. The reply is an
object keyed by sensor id:

```json
{
  "temperature": {
    "id": "temperature", "label": "Room temperature", "unit": "°C", "type": "float",
    "disable": false, "critical": false,
    "hardware": "DS18B20", "pin": 10, "connected": true, "address": "28FF640E2F1A3C02"
  },
  "door": {
    "id": "door", "label": "Door", "unit": "", "type": "boolean",
    "disable": false, "critical": true,
    "hardware": "reed switch on PCF8574", "pin": 4, "connected": true
  }
}
```

The backend reads `id`, `label`, `unit`, `disable` and `critical`. **The `id`
must equal the key the reading is published under.** `type` uses the backend's
enum — `int`, `float`, `string`, `boolean`. `hardware`, `pin`, `connected` and
`address` are for people and for the Dashboard; the backend ignores fields it
does not know.

The same object is the `sensors` block of the device descriptor (`INFO`) and
the reply to `SENSORS INFO`, so one function serves all three.

## What a sensor looks like in firmware

Plain functions and a status struct — the components are singletons, and a
struct copied under a spinlock is the whole thread-safety story:

```cpp
struct TempSensorStatus { bool connected; bool parasite; float tempC; uint32_t lastReadMs; char address[17]; };

void             setupTempSensor();       // starts sampling; returns immediately
TempSensorStatus tempSensorStatus();      // snapshot under lock
float            currentTemperature();    // NAN when unknown
void             tempSensorInfo(JsonObject into);    // its entry in the declaration
void             tempSensorReport(JsonObject into);  // its field(s) in the readings object
```

A sensor never blocks the caller. It either runs a non-blocking state machine
on a `Timers` callback, or owns a FreeRTOS task and sleeps through the
conversion. A sensor whose hardware is missing keeps looking — retried every
interval, reported once — rather than giving up at boot.

## Network sensors

A controller on one device often needs a reading that lives on another. An
air-conditioner controller needs the door; the door's reed switch is on the
light node across the room.

A network sensor is a sensor whose readings arrive from another device's
`sensors` topic. It has the same interface as a local one, so the controller
does not know or care where a reading comes from:

```cpp
NetworkSensor door;
door.bind("Mycroft", "door", /*invert*/ false);   // from config: door_device, door_key, door_invert
...
bool open;
if (door.asBool(open) && door.status().fresh) { ... }
```

- Bound by **runtime device name and key**, persisted in config as
  `<name>_device`, `<name>_key`, `<name>_invert`. These are the Dashboard's
  key names, on purpose, so the same sensor is described the same way
  everywhere.
- Fed by the loop-side router with every `<Other>/sensors` message; it expands
  the object and keeps only its own key.
- `fresh` goes false after 10 minutes without a reading, or on a retained
  `offline` from that device. A stale door is **unknown**, not closed.
- Boolean parsing accepts only `1`/`true`/`0`/`false`. A reed switch's polarity
  is part of the binding.

This is the Dashboard's sensor-binding logic with the UI removed. It is the
piece that lets a controller be written once and take its inputs from wherever
they happen to be.
