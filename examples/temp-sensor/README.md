# DS18B20 Resource device

This example uses the active NightMare Network Resource API around a pollable DS18B20 driver.

It exposes:

```text
temperature
    ManagedSensor<float>

connected
    ManagedSensor<bool>

sensor_address
    ManagedSensor<String>

rescan
    ManagedAction
```

The sensor driver remains application code. NightMare owns how those application facts participate in MQTT.

## Setup

1. Copy `include/creds.example.h` to `include/creds.h`.
2. Set WiFi, Local MQTT and Remote MQTT values.
3. If Remote MQTT is used, replace the placeholder `ROOT_CA` with the broker CA certificate.
4. Select the appropriate PlatformIO environment for the hardware.
5. Build/upload normally.

The DS18B20 data line requires an external 4.7 kΩ pull-up to 3.3 V; many breakout boards already include one.

## Runtime model

`TempSensor.cpp` is a cooperative state machine:

```text
start conversion
return to the application
wait without blocking
read after conversion time
schedule next reading
```

The application registers a MANAGED Scheduler callback every 100 ms to service that state machine.

A valid measurement updates:

```text
<device>/resources/temperature/state
```

`connected` reports whether a DS18B20 is currently available.

If the physical sensor disappears, `temperature` remains the last known Resource value; consumers that care about physical availability should use `connected`.

`sensor_address` is only published after a non-empty DS18B20 address is known, because an empty Resource String is reserved for retained-state deletion.

## Broker selection

Like the basic example, this project switches to Local MQTT from the first WiFi-connected callback for convenient local development.

Remove that callback if the project should keep the framework's normal remote-first behavior.
