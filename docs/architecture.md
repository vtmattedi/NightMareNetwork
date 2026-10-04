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

Devices may communicate through Local MQTT, Remote MQTT/TLS, or a local
ESP-NOW connection through the Nightmare Gateway (authenticated, encrypted
sessions; see [modules/espnow-protocol.md](modules/espnow-protocol.md)).

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
│ ConfigManager                    │
│ Scheduler                        │
│ Telemetry                        │
│ Command handling                 │
│ NmConnection / NmMessageRouter    │
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

It does not own MQTT publication or Resource cleanup. That prevents identity storage from becoming coupled to the connection implementation.

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
├── NetActionResource
│   ├── ManagedAction
│   └── RemoteAction
└── NetEventResource
    └── NetEvent<T>
        ├── ManagedEvent<T>
        └── RemoteEvent<T>
```

An Event is its own kind rather than a Value with a flag, so it does not inherit the Value-only state (availability, freshness, advertisement, hardware policy, optimistic state).

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

## ConfigManager

`ConfigManager` is a separate local registry for application configuration
parameters. The application owns each `Config<T>`; construction registers a
non-owning pointer with the function-local global manager and destruction
unregisters it. The manager provides `list`, `get`, `set`, and `manifest`
String ingress plus a compact MessagePack declaration manifest.

It has no connection to `ResourcesManager` or MQTT. The command layer routes
its `CONFIG ...` namespace to `configManager().handle(...)`; ConfigManager
itself remains connection-neutral. Persistence uses `PersistentSettings` under
reserved `_config:` keys. The ESP startup lifecycle calls `restore()` after
persistent storage initialization and before normal framework services start.

## Connection and MQTT layers

Networking is deliberately split into a NightMare connection coordinator and
protocol drivers.

Connectivity services form this dependency graph:

```text
WiFiRadio
 ├─ WiFiIP
 │   └─ MQTT
 └─ ESP-NOW
```

Each service owns only its lifecycle and reports `supported`, `enabled`, and a
common `ConnectivityState`. Transport preference is routing intent, not a
connectivity lifecycle request.

### NmConnection

`NmConnection` owns preferred and active NMNW transport selection, routing
failover, binary-safe generic publication, the shared subscription registry,
application message-handler registration, and Resource connection injection.
It observes connectivity state and never starts or stops WiFiIP, MQTT, or
ESP-NOW. Only one usable transport is active for framework traffic at a time.

### NmMqttConnection

`NmMqttConnection` owns the independently enabled MQTT service and adapts its
`MQTT` or `LOCAL_MQTT` broker profile to the shared ESP-IDF driver. WiFiIP is a
dependency: MQTT waits for it but never enables or disables it.

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

Traffic not consumed internally is offered to the generic application
`NightMare::OnMessage` handler. The callback belongs to the connection boundary,
so applications do not depend on the router or an MQTT-specific ingress API.

## Retained state model

Retained MQTT messages are used where the network needs current state rather than event history.

Current retained families include:

```text
<device>/status
<device>/info
<device>/hardware/msgpack
<device>/hardware/json        optional
<device>/telemetry/system
<device>/telemetry/network
<device>/telemetry/heartbeat
<device>/manifest/msgpack
<device>/manifest/json        optional
<device>/manifest/consume/msgpack
<device>/manifest/consume/json optional
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
NmMqttConnection
    │
    ▼
NmConnection
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

The Resource Manager consumes recognized Resource traffic even if the specific
operation fails. A malformed or rejected message for a known Resource does not
escape into another application ingress path.

## Message flow: custom application traffic

For a topic subscribed through `NightMare::Subscribe()` that is not owned by a
framework handler:

```text
connection message
    │
    ▼
NmMessageRouter
    │
    ├── Resources / time / console consume it -> stop
    │
    └── otherwise
          │
          ▼
    NightMare::OnMessage handler
```

The callback receives the full topic, binary payload plus explicit length, and
the retained flag reported by the active connection.

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

The application makes the domain decision. NightMare owns the connection and state contract.

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

/hardware/msgpack
    canonical reconstructable hardware configuration

/telemetry/system
    regular runtime health

/telemetry/network
    slower network bookkeeping

/telemetry/heartbeat
    configurable transient heartbeat
```

The individual static info sections remain queryable through the INFO API without multiplying retained MQTT topics.

Resources remain responsible for application state/freshness.

## Startup lifecycle

Normal application startup is:

```cpp
void setup()
{
    // Declare/bind application Configs, Resources, and handlers first.

    startNightMareESP();

    // Start application-specific hardware/services.
}
```

Binding Resources before framework startup matters because pending old-identity cleanup can only withdraw retained state for Resources that exist in the current firmware.

`startNightMareESP()` then coordinates enabled common infrastructure:

```text
DeviceIdentity.begin()
    initializes PersistentSettings and applies the persisted timezone

ConfigManager.restore()
    restores bound Config values or persists their firmware defaults

ResourcesManager.loadResourceSettings()
    restores Managed Value advertisement policy and hardware poll overrides

ResourcesManager.loadRemoteSources()
    restores Remote Resource source bindings

Scheduler.begin(...)
    TASK or MANUAL according to configuration

pending identity cleanup retry
    installed as a MANAGED callback job when needed

Telemetry.start()
    installs periodic callback jobs

WiFiRadioBegin()
    starts the shared driver/radio

WiFiBegin() / WiFiIP_enable()
    starts the explicitly enabled STA/IP service

Mqtt_enable() and EspNow_enable()
    start each independently when its dependency is ready

ConnectionBegin()
    selects one active usable NMNW transport without changing service lifecycle
```

The WiFi first-connect path reports IP state. MQTT reacts only when it is
already enabled. Automatic time synchronization starts the asynchronous ESP32
SNTP client; MQTT-assisted `Control/time` synchronization remains available as
an auxiliary path.

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
one Resource housekeeping step
completed SNTP synchronization events when time sync is enabled
one pending framework publication request
Scheduler when configured MANUAL
serial command resolver when enabled
```

The SNTP network callback itself runs on lwIP's task; `tickNightMareESP()` moves
the time-synchronized flag transition and the application time-sync callback
back into the normal cooperative context.

It also services Wi-Fi scan finalization so an ESP-NOW suspension is always
released after completion, failure, abort, or timeout.

## Reconnect lifecycle

On MQTT reconnect, NightMare restores framework participation instead of asking each application Resource to do so manually.

The reconnect callback restores subscriptions, records retained framework
publications, flushes already queued application messages, and invokes the
project callback. `tickNightMareESP()` then processes at most one queued
framework publication per call. Failed work moves behind the other ready work
and retries with exponential backoff capped at five minutes. A fresh request
resets that request's backoff and makes it ready immediately.

The path includes:

```text
default/framework subscriptions
Resource subscriptions
optional discovery subscriptions
online status publication
Resource manifest/state re-announcement
consume-manifest re-announcement
/info refresh
hardware configuration refresh
queued MQTT messages
project connected callback
```

The bounded tick path prevents manifests, Resource states, INFO, and hardware
documents from being built in one MQTT/TLS reconnect burst.

This is an example of the project's “register once, participate automatically” rule.

## Identity adoption and cleanup

Changing a device name is treated as a migration rather than a simple String assignment.

The new name is persisted, while the old name and the cleanup work still required are remembered.

If the network address is already locked for the current boot, the running device keeps using the old name until reboot.

Once the new identity is active, cleanup removes retained network state under the old identity, including:

```text
old retained managed Value states that are still declared
old status topic
```

Cleanup is retryable. DeviceIdentity stores the migration state; network/resource code performs the actual publication/deletion work.

## Feature composition

The library uses compile-time feature flags from `NightMareConfig.h` / `Features.h`.

Optional modules compile only when enabled, and invalid dependency combinations fail at compile time.

The active implementation is still ESP32-oriented. The feature system makes modules optional; it does not yet make the platform implementation fully portable.
