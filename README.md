# NightMare Network

NightMare Network is a small resource protocol and an ESP32 library for devices that need to discover and use each other's state and capabilities. A Device owns Resources; a Service implements behavior behind them. Values hold state, Actions request work, and Events report occurrences.

A minimum network needs two devices and one MQTT broker:

```text
Device A ── MQTT broker ── Device B
```

In a larger deployment, devices communicate through a cluster's local broker. Bridges carry selected traffic to a remote broker. The backend connects on the remote side; it does not connect to Local MQTT. Cluster is a topology boundary, while namespace is a separate logical address boundary.

## Start here

1. [Overview](docs/overview.md) explains the network and topology.
2. [Getting started](docs/getting-started.md) builds a device with one Value and one Action.
3. [Resources](docs/resources.md) covers registration, authority, discovery, metadata and callbacks.
4. [Protocol](docs/protocol.md) specifies the current MQTT mapping.
5. [Runtime and time](docs/runtime.md) covers Jobs, execution modes and clock synchronization.
6. [Services and platform](docs/services.md) covers telemetry, ESP32 platform helpers, WiFi and OTA.
7. [API reference](docs/reference.md) lists public entry points and limits.
8. [Device infrastructure and Console](docs/qol-restoration.md) covers settings, identity, operator commands and the standard facade.

The [basic](examples/basic) and [temperature sensor](examples/temp-sensor) projects are buildable PlatformIO examples. Both use `symlink://../..` so their builds test the current checkout.

## Repository

`src/NightMare` is the active C++ library. `Legacy/src` keeps the earlier TCP, timer, command, controller and configuration implementations as historical reference; it is outside the PlatformIO library build. `docs/` is the documentation source. `website/` renders it and `mcp/` exposes it to clients.

`#include <NightMare.h>` is the convenience include. The matching folder includes, such as `<NightMare/Resources/NetValue.h>`, are available when a smaller dependency surface is useful.
