---
title: Topics
description: Every MQTT topic a NightMare device publishes or consumes, what is retained, and how devices are discovered and named.
section: protocols
order: 10
---

# Topics

Every device owns a topic root equal to its name. `MQTT_Send()` prepends it;
`MQTT_Send_Raw()` does not. Everything below is under `<Device>/` unless marked
**raw**. Device names are case-sensitive: `micro` and `Micro` are two devices.

## Published by a device

| topic | payload | when | retained | who reads it |
| --- | --- | --- | --- | --- |
| `status` | `online` / `offline` | library, on connect; `offline` is the broker's last-will | **yes** | Dashboard, backend — a device with no retained status is offline |
| `telemetry` | `{"System":{...}}` — see [Telemetry](/docs/protocols/telemetry) | library, every 15 s | no | backend stores `.System`; Dashboard uses it as a sign of life |
| `sensors` | one JSON object, one field per sensor key | on change, and on a heartbeat | no | backend (history), Dashboard (registry), other devices (network sensors) |
| `info` | device descriptor: board, sensors, actuators, controllers | on connect, on `INFO` | no | Dashboard |
| `state` | the controller's state document | on change, on a heartbeat | no | the Service bound to this device |
| `console/out` | reply to a `console/in` command | library | no | whoever asked |
| `console/controlled/<id>/out` | MQTTP reply, chunked if long | library | no | the requester with that `<id>` |

Readings go on **`sensors` as one object**, never as bare scalars on
`sensors/<key>`. Both consumers accept the scalar form, but the object is one
message instead of N and carries a sensor's *absence* as `null`.

## Consumed by a device

| topic | published by | what happens |
| --- | --- | --- |
| `<Self>/console/in` | anyone | the library runs the command through the resolver |
| `all/console/in` | anyone | same, broadcast to every device |
| `<Self>/console/controlled/<id>/in` | Dashboard, backend | MQTTP request; reply goes to `.../out` |
| `<Other>/sensors` | another device | a network sensor picks its key out; nothing else reads other devices' readings |
| `<Other>/status` | another device | network sensors use it for liveness |
| `Control/time` **(raw)** | backend | consumed inside the MQTT client; sets the clock |

The library subscribes to `#`, so every message on the broker reaches the
device. The device must ignore everything not in this table, and must do so on
the MQTT task cheaply, by topic shape, before anything is allocated.

## Reserved roots

`Control/` and `n8n/` are the backend's own channels and are never devices.
`all/` is the broadcast console. A device never registers any of these as a
peer, and the backend keeps an operator-editable ignore list with the same
defaults.

## Retained messages and tombstones

Only `status` is retained by the library. Retained payloads are how a device
re-announces itself to a consumer that just connected — and how a deleted
device comes back from the dead.

An **empty retained payload is a tombstone**: the MQTT way of deleting a
retained message. Both consumers treat it as "gone", never as a new device.
When the backend or Dashboard deletes a device it clears `status`, `state`,
`ai_state` and every `sensors` topic it saw, because any one of them left
retained re-creates the device on the next subscribe.

## Identity channels and discovery

Three channels can *create* a device in a consumer: `status`, `state` and
`ai_state`. A reading from an unknown device is dropped, because it is usually
retained by something that was deleted.

A fresh device announces itself as `Esp32-nm-<efuse-mac>`. The backend's
adoption flow:

1. sees the factory name on an identity channel;
2. sends `ping` over MQTTP and waits for `PONG` — a device that never answers
   is left alone, not persisted;
3. reads `bootinfo`, `sensors`, `config get ALL -p`, `HARDWAREINFO`;
4. an operator names it: `CONFIG SET _device_name "<name>" -p`, which must
   reply with `"saved":true`;
5. clears the factory name's retained identity channels.

The same four reads happen again each time a known device comes back online,
so new firmware or a new sensor is picked up without a restart.

## The `Control` channel

| request (raw) | payload | reply (raw) | payload |
| --- | --- | --- | --- |
| `Control/request` | `time` | `Control/time` | `{"timestamp": <ms>, "offset": 0}` |
| `Control/request` | `forecast` | `Control/forecast` | `{icon, temp, feels, min, max, rain, condition}` |

Time sync is handled inside the library's MQTT client. The forecast is consumed
by the Dashboard only.
