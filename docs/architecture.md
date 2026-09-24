---
title: Architecture
description: How the core NightMare Network components fit together at runtime.
section: architecture
order: 10
---

# Architecture

NightMare Network is organized around a small set of responsibilities rather than one large device object.

The application owns hardware and behavior. NightMare owns the common infrastructure that lets that behavior participate consistently in an MQTT network.

## System view

A typical deployment looks like:

```text
                        remote/global side
                    ┌────────────────────────┐
                    │ backend / services      │
                    │ Remote MQTT             │
                    └───────────▲─────────────┘
                                │
                              bridge
                                │
                    ┌───────────▼─────────────┐
                    │ Local MQTT              │
                    │ cluster boundary        │
                    └──────▲────▲────▲────────┘
                           │    │    │
                        device device device
```

Devices communicate through the Local MQTT broker.

Selected traffic may be bridged to Remote MQTT for backend/global services. The backend belongs on the remote side rather than connecting directly to every local broker.

A cluster is a topology boundary. Namespace is a separate logical addressing concept and is not yet part of the current device-rooted topic format.

## Device view

Inside one device:

```text
Application
    │
    ├── declares/binds Resources
    ├── implements handlers
    └── owns hardware/business logic
    │
    ▼
┌──────────────────────────────────┐
│ NightMare Network                │
│                                  │
│ DeviceIdentity                   │
│ ResourcesManager                 │
│ Scheduler                        │
│ Telemetry                        │
│ Command handling                 │
│ MQTT facade / NmMessageRouter    │
│ NightMareESP lifecycle           │
└──────────────────────────────────┘
    │
    ▼
ESP32 / WiFi / MQTT
```

These pieces are intentionally separate. For example, DeviceIdentity stores migration state but does not publish MQTT cleanup itself; the network/resource layers perform the cleanup.

## DeviceIdentity

`DeviceIdentity` owns the device's MQTT address and identity state.

It exposes:

```text
deviceName
    current logical network identity

hardwareSignature
    stable generated identity of the physical board

deviceId
    raw machine-oriented hardware ID

timezone
    persisted POSIX timezone used for local-time presentation
```

It also owns timezone application plus the persistence state needed for timezone configuration, adoption, and old-identity cleanup.

It does not own MQTT publication or Resource cleanup. That prevents identity storage from becoming coupled to the transport implementation.

## Resources

The Resource layer is the application-facing network model.

```text
NetResource
├── NetValueResource
│   └── NetValue<T>
│       ├── ManagedSensor<T>
│       ├── RemoteSensor<T>
│       ├── ManagedState<T>
│       └── RemoteState<T>
└── NetActionResource
    ├── ManagedAction
    └── RemoteAction
```

The typed part of a Value ends at `NetValue<T>`. `ResourcesManager` sees a non-template boundary and encoded Strings.

That keeps MQTT routing, manifests, and subscription management independent of the application's C++ value type.

## ResourcesManager

`ResourcesManager` owns Resource participation and routing.

Its responsibilities include:

```text
bind / unbind
manifest publication
managed-state publication
remote subscriptions
reconnect restoration
/set handling
/invoke handling
remote state application
Resource command routing
manifest discovery/diagnostics
```

It does not own device identity. Resource topics are resolved using the Resource layer and the current DeviceIdentity.

It also does not own the Resources themselves. The application owns the Resource objects and must keep them alive while bound.

## MQTT layers

MQTT is deliberately split into layers.

### MQTT facade

`MQTT.cpp` is the project-facing facade.

It exposes conveniences for:

```text
publish
custom subscriptions
broker switching
discovery
project callbacks
status serialization
```

It also provides the transport adapters used by ResourcesManager.

### NmMqttEsp

`NmMqttEsp` owns the ESP MQTT client lifecycle.

It handles:

```text
client creation/destruction
broker URI selection
TLS certificate use for remote MQTT
control task
connect/disconnect events
MQTT Last Will
incoming packet assembly
```

It works with full MQTT topics and does not perform Resource semantics.

### NmMessageRouter

`NmMessageRouter` owns automatic NightMare protocol routing.

Resource traffic is offered to `ResourcesManager` first. Other framework-owned traffic such as time synchronization and console commands is handled there as enabled.

Traffic not consumed internally can then reach the project's generic MQTT callback.

## Retained state model

Retained MQTT messages are used where the network needs current state rather than event history.

Current retained families include:

```text
<device>/status
<device>/info
<device>/hardware
<device>/hardware/msgpack
<device>/telemetry/system
<device>/telemetry/network
<device>/manifest
<device>/manifest/msgpack
<device>/manifest/consume
<device>/manifest/consume/msgpack
<device>/resource/<value>/state
```

Write requests and Action invocations are transient:

```text
<device>/resource/<value>/set
<device>/resource/<action>/invoke
```

This distinction is intentional. A state topic answers “what is true now?” A request topic asks work to happen now.

## Message flow: remote Value state

For a bound Remote Value:

```text
MQTT message
    │
    ▼
NmMqttEsp
    │
    ▼
MQTT.cpp
    │
    ▼
NmMessageRouter
    │
    ▼
ResourcesManager
    │
    ▼
NetValue<T>
    │
    ▼
application onUpdate callback
```

The Resource Manager consumes recognized Resource traffic even if the specific operation fails. A malformed or rejected message for a known Resource does not leak into the application's generic MQTT callback.

## Message flow: write to a ManagedState

```text
<device>/resource/<name>/set
    │
    ▼
ResourcesManager
    │ decode
    ▼
ManagedState<T>::onWrite
    │
    ├── false -> reject
    │
    └── true
          │
          ▼
      authoritative state changes
          │
          ▼
      retained /state publication
```

The application makes the domain decision. NightMare owns the transport and state contract.

## Message flow: RemoteState write

When application code calls `setValue()` on a RemoteState:

```text
application request
    │
    ├── publish /set to owner
    │
    └── open optimistic window
             │
             ▼
       effective local value
```

The owner's `/state` remains authoritative.

Optimism only covers the temporary gap between local intent and remote confirmation. It does not transfer ownership.

## Scheduler architecture

The Scheduler uses one job representation and one timing engine.

A job has:

```text
clock        Wall | Monotonic
interval     0 = one-shot, otherwise recurring
target       command | callback
scope        MANAGED | USER
```

The execution mode is independent of the jobs:

```text
TASK
    Scheduler owns a FreeRTOS task

MANUAL
    cooperative code services tick()
```

Only wall-clock String command jobs are persisted. Runtime callback pointers and monotonic deadlines are not.

USER jobs are the operator-visible subset. MANAGED jobs are protected from `JOB LIST`, `JOB DELETE`, and `JOB CLEAR`.

## Telemetry architecture

Telemetry is separated by lifecycle rather than by every possible category.

```text
/info
    mostly static / boot-scoped aggregate

/hardware, /hardware/msgpack
    hardware-only topology, readable and compact encodings

/telemetry/system
    regular runtime health

/telemetry/network
    slower network bookkeeping
```

The individual static info sections remain queryable through the INFO API without multiplying retained MQTT topics.

Resources remain responsible for application state/freshness.

## Startup lifecycle

Normal application startup is:

```cpp
void setup()
{
    // Declare/bind application Resources and handlers first.

    startNightMareESP();

    // Start application-specific hardware/services.
}
```

Binding Resources before framework startup matters because pending old-identity cleanup can only withdraw retained state for Resources that exist in the current firmware.

`startNightMareESP()` then coordinates enabled common infrastructure:

```text
DeviceIdentity.begin()
    loads and applies the persisted timezone

Scheduler.begin(...)
    TASK or MANUAL according to configuration

pending identity cleanup retry
    installed as a MANAGED callback job when needed

Telemetry.start()
    installs periodic callback jobs

WiFi_Auto()
    starts the WiFi/network path
```

The WiFi first-connect path starts other enabled network services according to feature configuration. Automatic time synchronization now starts the asynchronous ESP32 SNTP client; MQTT-assisted `Control/time` synchronization remains available as an auxiliary path.

## Runtime lifecycle

Normal loop code is:

```cpp
void loop()
{
    tickNightMareESP();

    // application cooperative work
}
```

`tickNightMareESP()` is intentionally small.

It services components that require cooperative dispatch, currently including:

```text
completed SNTP synchronization events when time sync is enabled
one pending framework publication request
Scheduler when configured MANUAL
serial command resolver when enabled
```

The SNTP network callback itself runs on lwIP's task; `tickNightMareESP()` moves
the time-synchronized flag transition and the application time-sync callback
back into the normal cooperative context.

It does not otherwise poll systems that already own their own lifecycle, such as MQTT or WiFi.

## Reconnect lifecycle

On MQTT reconnect, NightMare restores framework participation instead of asking each application Resource to do so manually.

The reconnect callback restores subscriptions, records retained framework
publications, flushes already queued application messages, and invokes the
project callback. `tickNightMareESP()` then processes at most one queued
framework publication per call. Failed work is requested again.

The path includes:

```text
default/framework subscriptions
Resource subscriptions
optional discovery subscriptions
online status publication
Resource manifest/state re-announcement
consume-manifest re-announcement
/info refresh
hardware JSON/MessagePack refresh
queued MQTT messages
project connected callback
```

The bounded tick path prevents manifests, Resource states, INFO, and both
hardware encodings from being built in one MQTT/TLS reconnect burst.

This is an example of the project's “register once, participate automatically” rule.

## Identity adoption and cleanup

Changing a device name is treated as a migration rather than a simple String assignment.

The new name is persisted, while the old name and the cleanup work still required are remembered.

If the network address is already locked for the current boot, the running device keeps using the old name until reboot.

Once the new identity is active, cleanup removes retained network state under the old identity, including:

```text
old Resource manifest
old consume manifest
old retained managed Value states that are still declared
old status topic
```

Cleanup is retryable. DeviceIdentity stores the migration state; network/resource code performs the actual publication/deletion work.

## Feature composition

The library uses compile-time feature flags from `NightMareConfig.h` / `Features.h`.

Optional modules compile only when enabled, and invalid dependency combinations fail at compile time.

The active implementation is still ESP32-oriented. The feature system makes modules optional; it does not yet make the platform implementation fully portable.
