# Basic Resource device

This example uses the active NightMare Network API.

It exposes:

```text
uptime_s
    ManagedSensor<uint32_t>

brightness
    ManagedState<uint8_t>, accepted range 0..100

identify
    ManagedAction
```

The application declares and binds the Resources once. NightMare handles the retained manifest, retained Value state, `/set` and `/invoke` routing, reconnect subscriptions, status, INFO/telemetry and command plumbing.

## Setup

1. Copy `include/creds.example.h` to `include/creds.h`.
2. Set WiFi, Local MQTT and Remote MQTT values.
3. If Remote MQTT is used, replace the placeholder `ROOT_CA` with the broker CA certificate.
4. Build with `pio run -e esp32c3-supermini` or `pio run -e esp32doit-devkit-v1`.
5. Upload/monitor with the usual PlatformIO targets.

The project uses this repository checkout through:

```ini
symlink://../..
```

## What to try

Once connected, inspect:

```text
<device>/resources
<device>/resources/uptime_s/state
<device>/resources/brightness/state
```

Request a brightness change:

```text
<device>/resources/brightness/set
```

with payload:

```text
75
```

Invoke:

```text
<device>/resources/identify/invoke
```

with an empty payload.

The example also enables the serial console, so commands such as:

```text
PING
INFO
INFO SYSTEM
JOB LIST
```

can be entered over Serial.

## Broker selection

The standard ESP lifecycle initializes Remote MQTT on the first WiFi connection. This example registers a WiFi callback that immediately switches to Local MQTT so a local development broker is the default demonstration path.

Remove that callback if the project should keep the framework's normal remote-first behavior.
