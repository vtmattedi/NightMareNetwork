---
title: Responsibilities
description: What NightMare Network owns and what remains application responsibility.
section: overview
order: 30
---

# Responsibilities

NightMare Network is most useful when the boundary between framework and application code stays clear.

The framework standardizes how a device participates in the network. The application still defines what the device actually does.

## NightMare provides

### Identity and addressing

NightMare owns the device's network identity and topic root.

It provides:

- the current adoptable device name,
- a stable physical hardware signature,
- a raw device ID,
- a persisted POSIX timezone for local-time presentation,
- topic construction relative to the current device,
- address locking,
- adoption state,
- cleanup of retained data left behind by a previous name.

The application should not construct its own competing identity system for normal NightMare traffic.

### Resource participation

Once Resources are declared and bound, NightMare owns the repetitive network behavior around them:

- registration,
- canonical Resource topics,
- retained manifests,
- retained consume manifests derived from Remote Resources,
- retained Value state,
- `/set` routing,
- `/invoke` routing,
- subscriptions for Remote Resources,
- reconnect re-announcement,
- Remote Value freshness,
- remote-source changes,
- basic manifest compatibility diagnostics.

The application owns the meaning of those Resources.

### MQTT conventions and routing

NightMare defines the standard topic families and routes framework traffic before it reaches the application's generic MQTT callback.

It manages:

- local vs remote broker selection,
- MQTT client lifecycle,
- framework subscriptions,
- Resource transport integration,
- console/control routing,
- optional discovery subscriptions,
- retained status,
- Last Will.

Applications can still publish and subscribe to custom MQTT topics when needed.

### Scheduler

NightMare provides one timing engine for:

- wall-clock jobs,
- monotonic jobs,
- one-shot work,
- recurring work,
- String commands,
- function-pointer callbacks.

It also separates:

```text
MANAGED jobs
    framework/application C++ jobs

USER jobs
    jobs created through the JOB command surface
```

The operator-facing JOB commands cannot delete MANAGED jobs.

### Commands

NightMare provides a common command path for framework/operator tasks.

Built-in families handle framework functions. Application commands can be delegated to the application's resolver when the built-ins do not consume the command.

This avoids implementing separate command grammars for serial, MQTT console, and correlated request/response paths.

### Device information and telemetry

NightMare owns the standard device-level information surfaces:

```text
<device>/status
<device>/info
<device>/hardware
<device>/hardware/msgpack
<device>/telemetry/system
<device>/telemetry/network
```

These cover identity/presence, hardware/build information, runtime health, and network bookkeeping.

They intentionally do not duplicate application Resource state.

### Common ESP32 lifecycle

`startNightMareESP()` centralizes common framework startup.

Depending on enabled features, that includes identity, Scheduler startup, pending identity-cleanup retry, telemetry scheduling, and the WiFi/MQTT path.

`tickNightMareESP()` is the common cooperative service point for framework pieces that require polling.

The application does not need to know whether the Scheduler is task-driven or manual when it uses the standard lifecycle.

## The application provides

### Hardware drivers

NightMare does not know how to operate application hardware.

The application remains responsible for code such as:

```text
IR transmit/receive
temperature probes
door switches
relays
motors
displays
custom buses
device-specific peripherals
```

### Sensor acquisition

A `ManagedSensor<float>("temperature")` does not measure temperature.

The application reads the sensor and updates the Resource.

NightMare handles how that state participates in the network.

### Actuator behavior

A writable state can receive a decoded request, but the application decides how that request maps to hardware.

For example, a ManagedState may represent:

```text
power
target_temperature
fan_mode
```

The application's write handler decides whether a request is valid and performs the required device operation.

### Action implementation

A `ManagedAction` defines a network-visible operation and optional argument metadata.

The application implements the actual operation in its handler.

NightMare routes the invocation and exposes the schema; it does not invent the business logic.

### Business rules

Rules such as:

```text
do not run AC while the door is open
limit temperature setpoint
retry a physical operation
debounce a sensor
coordinate several local peripherals
```

belong in application code unless they are generic enough to become an explicit framework feature.

### Resource declarations

The application chooses which capabilities are Resources and how they are named.

That is the contract the device exposes to other devices and tooling, so names should describe application concepts rather than transport details.

### Application-specific services

NightMareESP should not become a container for every device-specific service.

Application startup still owns things such as:

```text
setupTempSensor()
startIrServices()
startAcController()
```

The framework lifecycle should only contain reusable framework infrastructure.

## Shared boundary

Some responsibilities deliberately meet at an explicit boundary.

### ManagedState

NightMare:

- decodes the requested Value,
- routes it to the correct Resource,
- keeps Resource state semantics.

Application:

- accepts or rejects the request,
- performs the device-specific work.

### ManagedAction

NightMare:

- identifies the Action,
- provides the canonical payload,
- optionally validates declared argument metadata,
- routes execution.

Application:

- interprets the operation,
- performs the work,
- returns the `ActionResult`.

### RemoteState

NightMare:

- publishes the write request,
- maintains the optimistic window,
- tracks authoritative owner state.

Application:

- decides when it wants to request a new state,
- reacts to the effective value.

### Scheduler

NightMare:

- owns timing,
- storage rules,
- job dispatch,
- USER/MANAGED separation.

Application:

- supplies the callback or command to run,
- decides what the scheduled work means.

## Boundary rule

A useful test is:

> If two unrelated device projects would otherwise reimplement the same network/runtime plumbing, it probably belongs in NightMare. If it expresses what one particular device actually does, it probably belongs in the application.

That boundary is intentionally not perfect. The framework can grow when repeated application patterns become clear, but it should not absorb speculative abstractions before a real use case exists.
