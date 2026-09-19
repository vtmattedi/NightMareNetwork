---
title: Services and platform
description: Expose behavior through Resources and drive ESP32 adapters from Runtime.
section: services
order: 1
---

# Services and platform

A Service implements behavior through the Resources it registers. `TelemetryService` is common infrastructure: it registers uptime, heap, chip, boot and firmware Values and updates them through Runtime. `Esp32Device` starts it automatically unless telemetry is disabled.

The optional `GpioLightService` is a concrete GPIO example under Platform. It owns a writable `NetValue<bool>` and a `NetAction<void>`; a remote UI needs those Resources, not the C++ helper.

```cpp
// Include <NightMare/Platform/GpioLightService.h> explicitly.
GpioLightService light(2);
light.begin(device.resources(), false);
// Optional local behavior:
light.set(true);
```

The active Platform folder contains:

| Adapter | Behavior |
| --- | --- |
| `WifiStation` | Start WiFi and poll connection/reconnect state; creates no task. |
| `OtaService` | Start ArduinoOTA explicitly and poll `tick()`; creates no task. |
| `Esp32SystemInfo` | Read hardware, boot and runtime structs without Network or Resources. |
| `Esp32Device` | Wire settings, identity, Runtime, optional WiFi/MQTT/OTA, Console and telemetry. |

`MqttTransport` belongs to Network. Attach it to a `Network`, then call `begin(uri, user, password, certificate)` after WiFi is connected. Use `mqtt://` for a cluster's local broker or `mqtts://` with a trusted certificate for a remote broker. The backend belongs on the remote side; a bridge carries selected traffic between brokers.

To switch brokers after disconnection, call `configureFailover(primaryUri, backupUri, switchAfterMs)` before `begin`, then poll `tick()` on Runtime's thread. `Esp32Device` wires this from `mqttBackupUri`. The transport restores Resource and Console subscriptions when either broker connects.

A service can expose one Value for readable state, an Action for an operation without readable state, and an Event for a transient occurrence. Optional metadata helps generic clients render a control. Grouping metadata is separate from resource identity, so a service does not need to put `light/` inside every resource ID.
