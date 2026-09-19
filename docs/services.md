---
title: Services and platform
description: Expose behavior through Resources and drive ESP32 adapters from Runtime.
section: services
order: 1
---

# Services and platform

A Service implements behavior. Its public network interface is the Resources it registers. The built-in `LightController` owns a writable `NetValue<bool>` and a `NetAction<void>`; it handles requests by changing a GPIO and then publishing the actual state through `ResourceManager`. A remote UI needs the schema and those two resource IDs, not the `LightController` class.

```cpp
LightController light(2);
light.begin(device.resources(), false);
// Optional local behavior:
light.set(true);
```

The active Platform folder contains:

| Adapter | Behavior |
| --- | --- |
| `WifiStation` | Start WiFi and poll connection/reconnect state; creates no task. |
| `OtaService` | Start ArduinoOTA explicitly and poll `tick()`; creates no task. |

`MqttTransport` belongs to Network. Attach it to a `Network`, then call `begin(uri, user, password, certificate)` after WiFi is connected. Use `mqtt://` for a cluster's local broker or `mqtts://` with a trusted certificate for a remote broker. The backend belongs on the remote side; a bridge carries selected traffic between brokers.

A service can expose one Value for readable state, an Action for an operation without readable state, and an Event for a transient occurrence. Optional metadata helps generic clients render a control. Grouping metadata is separate from resource identity, so a service does not need to put `light/` inside every resource ID.
