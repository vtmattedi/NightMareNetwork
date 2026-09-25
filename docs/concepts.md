---
title: Core concepts
description: The vocabulary used throughout NightMare Network.
section: overview
order: 20
---

# Core concepts

This page defines the terms used across the NightMare Network documentation. These definitions are part of the architecture: other pages should reuse them rather than inventing nearby meanings.

## Device

A **Device** is one running NightMare node.

In the current protocol, the device name is the root of its MQTT topics:

```text
<device>/...
```

The current topic format does not include namespace.

## Device identity

NightMare keeps the logical name, physical identifiers, and local timezone together as device identity because they describe how a node is addressed and presented.

### Device name

The **device name** is the logical network identity.

Example:

```text
bedroom-ac
```

It is used in MQTT addressing and may be changed through identity adoption.

### Hardware signature

The **hardware signature** is the generated name the physical board was born with.

Example:

```text
Esp32-nm-6ca172e0
```

It remains stable when the logical device name is adopted.

### Device ID

The **device ID** is the raw machine-oriented hardware identifier.

It is useful where a stable low-level identifier is needed without treating it as the human/network name.

### Timezone

The **timezone** is the persisted POSIX `TZ` string used for local-time presentation.

It changes formatting and local wall-clock interpretation; Unix epoch values remain UTC-based.

## Resource

A **Resource** is an application capability exposed to the NightMare network.

There are two kinds:

```text
Value
Action
```

Resources belong to the application layer. NightMare provides their registration, addressing, discovery, state routing, subscriptions, and transport behavior.

## Value

A **Value** is a typed piece of state.

At the C++ layer, `NetValue<T>` owns the typed value behavior. The Resource Manager sees only the non-template `NetValueResource` boundary and encoded Strings.

Typical examples are:

```text
temperature
door_open
power
target_temperature
mode
```

Value state is published at:

```text
<device>/resource/<name>/state
```

and is retained.

An empty retained payload is a tombstone: it deletes the retained Value state. Empty String is therefore not a valid Resource String value.

## Action

An **Action** represents an operation rather than stored state.

Example:

```text
learn_ir
reboot_controller
start_pairing
```

An Action may publish runtime argument metadata in the manifest, but it does not have Value freshness or `/state`.

Invocation uses:

```text
<device>/resource/<name>/invoke
```

Ordinary MQTT invocation is fire-and-forget. The transport accepting the message is not proof that the remote Action executed successfully.

When the result matters, use a correlated command/MQTTP path that can preserve the local `ActionResult`.

## Managed and Remote

Every Resource has a permanent role.

### Managed

A **Managed** Resource is implemented by the current device.

Examples:

```cpp
ManagedSensor<float>
ManagedState<bool>
ManagedAction
```

A Managed Resource's owner is always the current `DeviceIdentity`.

### Remote

A **Remote** Resource is implemented by another device.

Examples:

```cpp
RemoteSensor<float>
RemoteState<bool>
RemoteAction
```

A Remote Resource is declared with a stable local name. It may start without a
source and be pointed at a remote `OWNER/RESOURCE` later.

Changing its source changes only the target device/resource. It does not change
the local name or the Resource's role.

## READ and READ_WRITE

`AccessPolicy` describes whether a Value can only be observed or can also receive write requests.

```text
READ
    observe only

READ_WRITE
    observe + accept /set requests
```

The standard wrappers choose the normal policy:

```text
ManagedSensor   -> READ
RemoteSensor    -> READ
ManagedState    -> READ_WRITE
RemoteState     -> READ_WRITE
```

## Authoritative state

For a Value, **authoritative state** is the owner's last reported value.

For Managed Values, the local device is the owner.

For Remote Values, authoritative state arrives from the remote owner's retained/live `/state` topic.

The API keeps this separate from an optimistic local value so applications can distinguish “what the owner last said” from “what the application currently sees.”

## Freshness

A Value can be:

```text
UNKNOWN
FRESH
STALE
```

Freshness belongs to Value state.

The retained Resource manifest does not make a Value fresh. Device presence does not make a Value fresh. `/state` is the signal that carries the authoritative Value.

This is one of the project's central rules:

> **A manifest describes. `/state` tells the truth.**

## STRICT and OPTIMISTIC synchronization

Remote Values can have different read-after-write behavior.

### STRICT

A STRICT Resource always reports the owner's authoritative state.

`RemoteSensor<T>` uses STRICT behavior because a read-only Resource has no local write to shadow.

### OPTIMISTIC

A RemoteState uses an optimistic window after a local write.

During that window, the application can observe the locally requested value immediately while the owner's previous state may still be in flight.

When the owner catches up, or the window expires, authoritative state wins again.

Optimism does not change ownership. The remote device remains the source of truth.

## Manifest

The retained topic:

```text
<device>/manifest
```

contains the device's Resource manifest.

The manifest is descriptive. It exists for discovery, self-description, tooling, and compatibility diagnostics.

It does not gate Resource state. A missing, stale, incompatible, or withdrawn manifest does not by itself invalidate a `/state` message.

The separate retained `<device>/manifest/consume` document describes valid,
bound Remote Resource dependencies. It does not add Remote Resources to the
provider manifest.

## State

A Value's retained state lives at:

```text
<device>/resource/<name>/state
```

For Remote Values, receiving owner state updates authoritative state and freshness.

For Managed Values, publishing state exposes the owner's current truth to the network.

## Set

A write request to a writable Value uses:

```text
<device>/resource/<name>/set
```

For a ManagedState, the application handler decides whether the decoded requested value is accepted.

If accepted, the requested value becomes authoritative state.

## Invoke

An Action request uses:

```text
<device>/resource/<name>/invoke
```

A ManagedAction routes the canonical payload to its application handler.

A RemoteAction publishes the invocation to the device that owns the Action.

## Command

A **Command** is text handled by the NightMare command system.

Commands are operator/framework operations such as:

```text
INFO
TIME
JOB
MQTT
WIFI
CONFIG
REBOOT
```

Applications may also provide their own command resolver.

Commands and Resource Actions remain different abstractions: a Resource Action is part of the application's capability model, while a command is part of the command/control surface.

A command whose first non-whitespace character is `>` is a Resource-routing expression. It is an operator/control adapter into already-bound Resources; it does not create a second Resource model or change the Resource MQTT protocol.

## Status

`<device>/status` is retained presence plus minimal identity.

Its job is to answer:

```text
Which logical device is this?
Which physical board is behind that name?
Which timezone does it use for local time?
Is it online?
```

The MQTT Last Will uses the same JSON shape as normal status publication.

Status is not the freshness mechanism for Resources.

## Info

`<device>/info` is retained, mostly static information whose lifecycle is boot-scoped or effectively immutable during a boot.

It aggregates sections such as:

```text
identity
hardware
build
boot
```

The sections can be queried individually through the INFO interface without creating a separate MQTT topic for each section.

## Hardware topology

Hardware wiring is published separately from `/info` because it has its own
compact representation. The two retained forms are:

```text
<device>/hardware
<device>/hardware/msgpack
```

They describe physical board instances and models, which board owns each
integrated device, optional semantic device kinds and physical-form slugs,
electrical nets, explicit physical segments, directions, pull modes, optional
pull resistors, and active-low behavior.
Artwork, footprints, coordinates, icons, and other rendering data remain
server-side.

## Telemetry

Telemetry is runtime framework/device bookkeeping.

Current retained documents are:

```text
<device>/telemetry/system
<device>/telemetry/network
```

System telemetry covers runtime health such as uptime and heap information.

Network telemetry covers slower-changing network state such as WiFi connectivity, IP, RSSI, MQTT connectivity, and which broker is active.

Application sensor state belongs to Resources instead.

## Scheduler job

A Scheduler job combines several independent choices.

### Clock

```text
Wall
Monotonic
```

Wall jobs use Unix time. Monotonic jobs use the current boot's `millis()` timeline.

### Repeat

```text
one-shot
recurring
```

An interval of zero means one-shot.

### Execution target

A job executes exactly one target:

```text
String command
callback
```

Callbacks are plain function pointers, which includes non-capturing lambdas.

### Run mode

The Scheduler can be driven by:

```text
TASK
MANUAL
```

TASK mode creates a FreeRTOS task that calls `tick()` periodically.

MANUAL mode creates no Scheduler task; cooperative code must call `tick()`.

### Scope

Jobs also have an ownership scope:

```text
MANAGED
USER
```

`MANAGED` jobs are created by framework or application C++ code.

`USER` jobs are created through the `JOB` command interface.

Operator `JOB LIST`, `JOB DELETE`, and `JOB CLEAR` operate only on USER jobs, so they cannot delete framework/application jobs.

## Cluster

A **cluster** is a network/topology boundary centered around a Local MQTT broker.

It is not the same as namespace.

## Namespace

A **namespace** is a logical address/isolation concept.

Namespace is part of the intended architecture but is not yet encoded in the current MQTT topic structure.
