---
title: NightMare Network
description: Why NightMare Network exists, what it standardizes, and the philosophy behind it.
section: overview
order: 10
---

# NightMare Network

NightMare Network is a small C++ framework and a set of MQTT conventions for ESP32 devices that need to participate in the same local network.

The project started from a recurring problem: the interesting part of an embedded project is usually the device itself, but every networked device ends up rebuilding the same surrounding machinery. It needs an identity, an MQTT address, reconnect behavior, retained state, discovery, remote control, scheduling, operator commands, telemetry, and enough conventions that another device or a backend can understand what it exposes.

NightMare makes those repeated concerns part of the framework.

The goal is not to turn every device into the same application. The goal is to make different applications participate in the network in the same way.

## Philosophy

The central rule is:

> **Register once, participate automatically.**

A Resource should not need custom MQTT code in every application. Once a Resource is declared and bound, the framework should know how to:

- publish its description,
- publish or consume its state,
- subscribe to the correct topics,
- restore those subscriptions after reconnect,
- route writes and invocations,
- and present the Resource consistently to other devices and tooling.

The same principle applies elsewhere. Device identity is owned in one place. Scheduler timing is implemented once. Telemetry has one owner. Common ESP32 startup is centralized instead of copied into every project.

This does **not** mean “hide everything.” NightMare deliberately keeps important semantics visible: who owns a value, whether a remote write is optimistic, whether a scheduled job survives reboot, and whether a message is retained are part of the programming model.

## The network model

The normal deployment model has a local MQTT broker inside a device cluster:

```text
Device A ─┐
Device B ─┼── Local MQTT ── bridge ── Remote MQTT ── backend/services
Device C ─┘
```

The local broker is the cluster boundary. Devices inside the cluster can communicate without requiring the remote backend to be reachable.

The backend belongs on the remote side. It does not need to connect directly to each cluster's Local MQTT broker.

A **cluster** is a topology boundary. A **namespace** is a logical addressing boundary. They are intentionally different concepts. The current topic format is still device-rooted and does not yet include namespace as part of the MQTT address.

## The core model

A NightMare device is built from a few cooperating pieces:

```text
DeviceIdentity
    names the device on the network

Resources
    describe application capabilities and state

ResourcesManager
    binds Resources to transport and routes Resource traffic

MQTT / NmMessageRouter
    moves messages and dispatches NightMare protocol traffic

Scheduler
    runs delayed or recurring command/callback work

Telemetry / INFO
    exposes device description and runtime health

NightMareESP
    starts common framework services and provides one cooperative tick point
```

Application code sits above those pieces. It declares Resources and implements what they mean.

## Resources are the application contract

NightMare models application-facing capabilities as Resources.

Values represent state. Actions represent operations.

Examples:

```text
temperature
door_open
ac_power
target_temperature
set_timer
learn_ir_code
```

A Resource can be **Managed** by the current device or **Remote**, meaning it points at a Resource implemented by another device.

That distinction is permanent for the Resource object. Retargeting a Remote Resource changes what it points to; it does not turn it into a Managed Resource.

The network representation is intentionally small:

```text
<device>/resources
<device>/resources/<name>/state
<device>/resources/<name>/set
<device>/resources/<name>/invoke
```

The manifest describes Resources. Retained `/state` is the authoritative freshness signal for Values.

## Identity and presence

A device has three related identities:

- a logical device name used in MQTT topics,
- a stable hardware signature generated from the physical board,
- a raw machine-oriented device ID.

The logical name may be adopted. The hardware signature does not change when the device is renamed.

Presence is published separately from application state:

```text
<device>/status
```

The status document is retained and has the same JSON shape for online state, graceful shutdown, and MQTT Last Will.

Resource freshness does not depend on status. A device being online and a particular Resource having fresh state are different facts.

## Static information and telemetry

NightMare separates information by lifecycle.

```text
<device>/info
    mostly static / boot-scoped description

<device>/telemetry/system
    runtime system health

<device>/telemetry/network
    network bookkeeping
```

Application sensor/state data does not belong in telemetry. It belongs in Resources, where it already has ownership and freshness semantics.

## Commands and Actions are different

NightMare has an operator/framework command system and a Resource Action system.

Commands are used for things such as:

```text
INFO
JOB
MQTT
WIFI
CONFIG
REBOOT
```

Resource Actions represent application capabilities exposed by a device.

That separation is intentional. “Reconfigure the framework” and “tell this device to perform an application operation” are different kinds of work even when both can ultimately travel over MQTT.

## Scheduling

The Scheduler has one timing engine with several independent dimensions:

```text
clock:
    wall
    monotonic

repeat:
    one-shot
    recurring

target:
    command
    callback

run mode:
    TASK
    MANUAL

scope:
    MANAGED
    USER
```

Framework/application C++ jobs are `MANAGED`. Jobs created through the operator `JOB` command are `USER`.

This boundary matters: an operator can list, delete, or clear USER jobs without accidentally deleting framework jobs such as telemetry publication.

Only wall-clock String command jobs persist across reboot. Callback pointers and monotonic deadlines are runtime state.

## Common ESP32 lifecycle

Applications normally register their resources and handlers, then call:

```cpp
startNightMareESP();
```

The framework starts enabled common services such as identity, Scheduler, telemetry, WiFi/MQTT, and identity-cleanup retry.

The application loop calls:

```cpp
tickNightMareESP();
```

for framework work that is intentionally cooperative. Components that already own a task or are event-driven are not duplicated there.

## What NightMare is not

NightMare is not a replacement for application logic, hardware drivers, or a general distributed database.

It also does not attempt to make every behavior transparent. Important limitations and trade-offs are documented explicitly, including the current ESP32 focus, the absence of namespace in topic addressing, the lack of a centralized runtime execution queue, and the deliberate use of plain function-pointer Scheduler callbacks.

The framework should remain small enough that those choices are understandable from the public model and the source.
