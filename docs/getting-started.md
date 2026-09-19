---
title: Getting started
description: Build an ESP32 device that exposes a Value and an Action.
section: getting-started
order: 1
---

# Getting started

Use Arduino core 3.x on ESP32. The repository's [basic example](../examples/basic) includes a PlatformIO configuration and a complete `main.cpp`. Copy `include/creds.example.h` to `include/creds.h`, then set WiFi and local MQTT credentials. Keep `creds.h` out of version control.

## One Value and one Action

```cpp
#include <NightMare.h>
using namespace NightMare;

MqttTransport transport;
NightMare::Network device("desk-lamp", transport);
Runtime runtime;
NetValue<bool> enabled("enabled", NetAccess::READ_WRITE);
NetAction<void> toggle("toggle", ActionResponse::ACK);

ActionStatus writeEnabled(void*, NetResource&, const String& payload) {
    bool next;
    if (!NetCodec<bool>::decode(payload, next)) return ActionStatus::INVALID_ARGUMENT;
    digitalWrite(2, next ? HIGH : LOW);
    device.resources().set(enabled, next);
    return ActionStatus::OK;
}

ActionStatus toggleEnabled(void*, NetResource&, const String& payload, String&) {
    if (payload.length()) return ActionStatus::INVALID_ARGUMENT;
    bool next = !enabled.get();
    digitalWrite(2, next ? HIGH : LOW);
    device.resources().set(enabled, next);
    return ActionStatus::OK;
}

void setup() {
    pinMode(2, OUTPUT);
    transport.attach(device);
    device.resources().add(enabled);
    device.resources().add(toggle);
    device.resources().onWrite(enabled, writeEnabled);
    device.resources().onAction(toggle, toggleEnabled);
    runtime.add([](void*) { device.tick(); });
    // Start WiFi, then call transport.begin("mqtt://broker:1883", user, password).
}

void loop() { runtime.tick(); }
```

`add` records both resources in the device schema. On MQTT connection, the manager publishes that schema and the current Value. An incoming write calls `writeEnabled`, where the application can validate and apply it. `set` publishes the accepted state. MQTT Action invocations and console text both reach `toggleEnabled` through the same registration.

## Complete examples

- [Basic](../examples/basic) shows WiFi, MQTT, a writable Value, read-only uptime and firmware Values, an Action, an Event, a Job and serial console ingress.
- [Temperature sensor](../examples/temp-sensor) shows a pollable DS18B20 driver, a read-only temperature Value, information Values, an Action and a transient Event.

The examples default to a local broker. To connect to a remote TLS broker, pass a `mqtts://` URI and a trusted certificate to `MqttTransport::begin`.
