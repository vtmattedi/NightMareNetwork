---
title: Known gaps
description: Known limitations, deferred designs, intentional non-goals, and technical debt.
section: architecture
order: 30
---

# Known gaps

This page is not a roadmap promise. It records places where the current implementation has a known boundary so users do not have to infer guarantees that do not exist.

The categories used here are:

```text
Known limitation
    the current implementation cannot do something useful

Deferred design
    the architecture has a likely direction, but it is intentionally not implemented yet

Intentional non-goal
    behavior the current design explicitly does not try to provide

Technical debt
    code works, but its implementation boundary should become cleaner
```

## Namespace is not in the current MQTT address

**Category:** Deferred design.

Current topics begin with:

```text
<device>/...
```

A namespace layer is part of the broader architecture, but it has not been added to the current Resource/device topic format.

Cluster and namespace should not be treated as synonyms: a cluster is the Local MQTT topology boundary; namespace is logical isolation/addressing.

## Config persistence is single-value and synchronous

**Category:** Intentional boundary.

Every accepted `Config<T>` mutation is written synchronously through
`PersistentSettings` before its runtime value is committed. There are no
multi-Config transactions, delayed writes, per-Config persistence flags, or
alternate persistence backends.

The shared store currently reserves 128 entries: 64 for the maximum Config
registry and 64 as deliberate framework/application headroom. This fixed
capacity trades RAM for a clear minimum persistence guarantee; it is not an
unbounded settings database.

`require_reboot` remains declaration metadata only; an accepted command value
is applied immediately and the manager takes no reboot action.

## No centralized Runtime execution queue

**Category:** Deferred design.

The current MQTT ingress path performs automatic routing through
`NmMessageRouter` and `ResourcesManager` directly from the MQTT callback path.
Reconnect publication is narrower: it uses typed `SystemRequest` bits and the
cooperative ESP tick to spread framework re-announcement across ticks.

The intended longer-term architecture may introduce a central Runtime/queue so connection ingress is cleanly separated from application execution.

A general execution queue is not implemented and should not be assumed by
application code. `SystemState` requests are a fixed framework publication
mechanism, not generic scheduling semantics.

## Resource auto-binding requires lifecycle separation

**Category:** Deferred design.

Resources still require explicit `bindResource()`. Applying Config-style
automatic registration safely requires splitting static-construction-safe
registry insertion from runtime activation, because current binding also locks
identity, validates Action schemas, restores settings/sources, subscribes, and
publishes. Actions additionally cannot register from their base constructor
before the leaf schema is fully constructed.

Advertisement refresh does not depend on that refactor. Dynamically bound
Managed Values restore advertisement policy immediately after the manager's
startup settings phase.

## Projected Remote Value JSON fan-out is not implemented

**Category:** Deferred design.

A future Remote Value may need to project one structured remote payload into several local typed views.

That design has been deliberately deferred rather than adding speculative complexity to the current Resource model.

Current Remote Values represent one remote Resource each.

## Scheduler callbacks have no capture/context abstraction

**Category:** Known limitation.

Scheduler callback jobs use:

```cpp
void (*)()
```

This supports plain functions and non-capturing lambdas.

Captured lambdas and `std::function` are not supported.

If a future use case requires framework-managed callback context, a small callback-plus-context representation may be considered. It is not part of the current API.

## Callback jobs do not survive reboot

**Category:** Intentional non-goal.

Function pointers are runtime/firmware state and are never persisted.

Even a wall-clock callback job disappears after reboot.

Persisted scheduling is for wall-clock String command jobs only.

## Monotonic jobs do not survive reboot

**Category:** Intentional non-goal.

A monotonic deadline is based on the current boot's `millis()` timeline and has no valid meaning after restart.

Monotonic jobs are runtime-only.

## Identity cleanup cannot remove unknown historical Resources

**Category:** Known limitation.

When a device adopts a new name, old-identity cleanup can delete:

```text
the old manifest
the old consume manifest
retained state for currently declared Managed Values
the old status topic
```

If an older firmware published a Managed Value that no longer exists in the current Resource registry, the current firmware has no historical record from which to reconstruct and delete that old retained topic.

## Status does not imply Resource freshness

**Category:** Intentional non-goal.

`<device>/status` reports device presence.

It does not assert that every Resource state is fresh, and Resource freshness should not be inferred from it.

Conversely, retained Resource state may still exist after a device goes offline.

## Network telemetry is last-known bookkeeping while offline

**Category:** Intentional trade-off.

`<device>/telemetry/network` is retained.

When the device disappears, that retained document is the last known network state. `<device>/status` is the authoritative presence signal.

NightMare does not currently rewrite network telemetry through MQTT Last Will.

## Heartbeat is advisory

**Category:** Intentional boundary.

`<device>/telemetry/heartbeat` is a configurable, non-retained periodic signal.
It does not replace retained status plus MQTT Last Will as authoritative device
presence, and it does not determine application Resource freshness.

## ESP32 is the active supported platform

**Category:** Known limitation.

The current library metadata targets:

```text
framework: Arduino
platform: espressif32
```

The source also includes ESP32-specific APIs and FreeRTOS behavior.

The feature system makes modules optional, but it is not yet a complete cross-platform abstraction.

## Persistence is coupled to the current implementation

**Category:** Technical debt.

`StateStore` currently layers persistent behavior over `RuntimeState` and uses the existing filesystem/settings implementation.

A cleaner `SettingsStore` plus platform persistence abstraction remains a possible later refactor.

Application code should use the public settings surface rather than depend on storage-file details or framework-private keys.

## Scheduler currently depends on Settings and Console at compile time

**Category:** Known implementation coupling.

The feature dependency rules currently require:

```text
Scheduler -> Settings
Scheduler -> Console
```

This reflects support for persisted wall jobs and String command dispatch.

A callback-only Scheduler could theoretically operate without both dependencies, but the current feature graph does not expose that narrower configuration.

## Automatic telemetry currently depends on Scheduler and Network

**Category:** Known implementation coupling.

Automatic telemetry publication is implemented using Scheduler jobs and the
active `NmConnection` publisher.

The current compile-time feature rules therefore require both Scheduler and
Network. MQTT is no longer a direct telemetry dependency.

A future design could separate “build/query telemetry JSON” from “automatically publish telemetry,” but that split is not part of the current feature model.

## ESP-NOW connection is declared but not implemented

**Category:** Deferred design.

`NmConnection`, lifecycle processing, Resource injection, and network telemetry
are connection-neutral. `ConnectionType::ESP_NOW` is reserved so callers and
telemetry do not need another enum change later, but selecting it currently
returns false without disturbing the active connection. There is deliberately
no ESP-NOW driver in this architecture pass.

## WiFi/time/OTA remain ESP-oriented services

**Category:** Technical debt / platform limitation.

WiFi orchestration uses `esp_wifi` and `esp_netif` directly, while time
synchronization and OTA remain ESP32/Arduino-platform services. The persisted
profile still uses the existing settings conventions.

They should be documented as platform services rather than treated as portable core abstractions.

## HTTP and WebSocket are optional legacy-adjacent surfaces

**Category:** Technical debt.

HTTP and WebSocket remain optional modules behind feature flags, but they have not received the same architecture pass as Resources, identity, Scheduler, and telemetry.

They should not be used as the model for new core architecture until revisited.

## LVGL utilities are peripheral to the core network model

**Category:** Technical debt / intentional boundary.

LVGL support remains optional and is not part of the core Resource/network architecture.

It should stay isolated behind its feature flag rather than shaping core APIs.

## No generic distributed transaction model

**Category:** Intentional non-goal.

NightMare does not attempt to provide cross-device transactions, consensus, or exactly-once distributed execution.

MQTT publication success means the local MQTT client accepted the publish. For remote Actions or writes, that is not equivalent to proof of remote execution.

Where a correlated result is required, use the correlated command/MQTTP path.

## No automatic backward compatibility for pre-freeze protocols

**Category:** Intentional non-goal.

The current architecture pass deliberately removes superseded compatibility paths rather than indefinitely carrying old representations.

Examples include:

```text
legacy Scheduler persistence import
literal "online" / "offline" status payloads
old monolithic telemetry document
old duplicate information commands
```

The stable documentation should describe the current model only.
