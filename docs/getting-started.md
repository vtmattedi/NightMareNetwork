---
title: Getting started
description: Build an ESP32 device with standard wiring and application Resources.
section: getting-started
order: 1
---

# Getting started

Use Arduino core 3.x on ESP32. The [basic example](../examples/basic) has a complete PlatformIO project. Copy `include/creds.example.h` to `include/creds.h`, set WiFi and MQTT credentials, and keep that file out of version control.

## Standard device

```cpp
#include <NightMare.h>
using namespace NightMare;

Esp32Device device;
NetValue<float> temperature("temperature");

void setup() {
    Serial.begin(115200);
    Esp32DeviceOptions options;
    options.wifiSsid = "my-wifi";
    options.wifiPassword = "my-password";
    options.mqttUri = "mqtt://broker:1883";
    options.serialConsole = &Serial;
    if (!device.begin(options)) return;
    device.resources().add(temperature, {true, 10000});
}

void loop() {
    device.tick();
    // When a sensor produces a reading:
    // device.resources().set(temperature, reading);
}
```

`Esp32Device` loads persistent settings, derives and persists a stable ID from the ESP32 hardware, constructs Network after identity is known, wires WiFi/MQTT, serial and MQTT Console, common telemetry and optional OTA. The application registers its own sensors, actuators, Actions and Events. `resources()`, `network()`, `runtime()`, `settings()`, `identity()` and `console()` remain accessible. The `Console` and telemetry accessors depend on their feature flags.

The optional `mqttBackupUri` and `brokerSwitchMs` options switch between a primary broker and a backup after sustained disconnection. A `mqtts://` URI requires a trusted certificate. Settings keys `wifi_ssid`, `wifi_password`, `mqtt_uri`, `mqtt_backup_uri`, `mqtt_user` and `mqtt_password` override the matching startup options after reboot. Changes to connection settings are read at the next `begin()`; no config callback changes a live connection. `device adopt new-id` also takes effect after restart.

## Low-level composition

Projects with their own lifecycle can construct `SettingsStore`, `DeviceIdentity`, `Runtime`, `Network`, `MqttTransport`, `Console`, `TelemetryService` and `WifiStation` separately. Load settings and identity before constructing `Network`; attach the transport and Console; poll Network, Console, WiFi and optional OTA with Runtime. The [temperature sensor example](../examples/temp-sensor) shows manual Resource and Runtime use. The standard facade uses the same ResourceManager and MQTT protocol.

## Console

Serial and `<device-id>/console/in` accept the same commands. MQTT replies appear on `<device-id>/console/out`. Examples:

```text
> toggle
> setColor 255 120 0
< sensor-device temperature 30000
< sensor-device
resources list
system info
config set location "Bedroom AC"
```

`>` invokes a registered Action. `<` reads a locally registered Value or lists Values mirrored for an owner; it shows the last known value and remote freshness. It does not issue a new network read: retained state and periodic Value publication provide that state. The owner must publish periodically if consumers require an age guarantee. Operator commands such as `config` are framework tools, not Actions. See [operator and storage details](qol-restoration.md).
