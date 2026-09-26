---
title: Design decisions
description: Intentional trade-offs and the reasoning behind the current architecture.
section: architecture
order: 20
---

# Design decisions

NightMare Network intentionally prefers a small number of explicit rules over a highly generic abstraction layer.

This page records decisions that might otherwise look arbitrary when reading the code.

Each entry states the decision, why it exists, and what it costs.

## Configs are local parameters, not Resources

**Decision:** firmware parameters use the independent local `ConfigManager`;
they do not inherit from Resource types or participate in Resource routing.

**Reason:** parameters such as timeouts and calibration offsets influence
device behavior but are not observable/controllable functional state. Giving
them Resource topics, freshness, ownership, and publication semantics would
misstate what they are.

**Consequence:** Config v1 has typed local values, explicit registration,
String command ingress, and a declaration manifest only. It has no MQTT,
persistence, automatic reboot, or Resource integration.

## Runtime facts and pending framework work are separate

**Decision:** `SystemState` uses typed flag and request enums backed by fixed bit
banks. Runtime facts answer what is true now. Requests answer which bounded
framework publication still needs processing.

**Reason:** module-neutral consumers should not depend on String keys or module
headers, and MQTT reconnect callbacks should not build every allocation-heavy
retained document in one TLS memory burst.

**Consequence:** `tickNightMareESP()` processes at most one ready publication
request per call. Failed work moves behind other ready requests and retries
with exponential backoff capped at five minutes. A fresh request resets its
backoff. This is task-safe framework plumbing, not an ISR API or a generic
scheduler. Generic transient String data continues to use an application-owned
`RuntimeState`.

## Retained MQTT state instead of a separate synchronization protocol

**Decision:** current state is represented with retained MQTT topics wherever MQTT already provides the required semantics.

Examples include:

```text
<device>/status
<device>/info
<device>/hardware
<device>/telemetry/system
<device>/telemetry/network
<device>/manifest
<device>/resource/<name>/state
```

**Reason:** MQTT already provides delivery, retained last-known state, subscriptions, broker fan-out, and reconnect behavior. Reimplementing a separate state synchronization layer would duplicate those mechanisms.

**Consequence:** the design inherits MQTT's retained-message model. A retained value is last known state, not a transaction log or proof that the publishing device is currently online.

## A manifest describes; `/state` tells the truth

**Decision:** Resource manifests are descriptive and diagnostic. Value freshness comes from `/state`.

**Reason:** discovery metadata and runtime state have different lifecycles. Coupling them would make Value validity depend on an unrelated metadata document arriving first or remaining current.

**Consequence:** a missing, incompatible, withdrawn, or malformed manifest does not automatically invalidate a Value state message. Consumers that need compatibility checks can use manifest information separately.

## Resource ownership is explicit

**Decision:** a Resource is declared as either `MANAGED` or `REMOTE`, and that role does not change.

**Reason:** ownership affects addressing, subscription direction, write semantics, and truth. Making it mutable would make a Resource's meaning depend on runtime history.

**Consequence:** retargeting a Remote Resource changes its source but never turns it into a Managed Resource.

## RemoteSensor is STRICT; RemoteState is OPTIMISTIC

**Decision:** read-only RemoteSensor values expose owner truth directly, while writable RemoteState values use a temporary optimistic local view after writes.

**Reason:** a read-only sensor has no local write gap to hide. A writable remote state does: the application may want immediate read-after-write behavior while the request travels to the owner and the new retained state comes back.

**Consequence:** an optimistic value is not authoritative. The owner remains the source of truth, and the local optimistic window eventually yields to owner state.

## Application state belongs to Resources, not telemetry

**Decision:** sensor/state/actuator data is not duplicated into a generic telemetry document.

**Reason:** Resources already carry ownership, types, access policy, retained state, subscriptions, and freshness. Duplicating the same values into telemetry would create two competing representations of application truth.

**Consequence:** consumers that want application state must consume Resources. Telemetry is framework/device bookkeeping.

## `/info` groups static data by lifecycle

**Decision:** `/info` aggregates identity, hardware facts, build information,
and boot-scoped information. Physical configuration is published separately as
retained JSON at `/hardware`.

**Reason:** physical configuration has its own validation and consumer
lifecycle, while ordinary device information remains compact device metadata.

**Consequence:** INFO does not contain connections. The previous positional
MessagePack hardware form is removed while the new schema stabilizes.

## Assemblies and connectors define physical topology

**Decision:** hardware configuration version 2 uses assemblies as the generic
physical composition primitive. Boards are assembly kinds. Devices expose
terminals, assemblies expose connector contacts, and connections store physical
conductors. Nets are inferred from connected components.

**Reason:** a flat board/device/net model could not reconstruct enclosure,
probe, panel, module, and direct-wire boundaries without hidden assumptions or
manually maintained derived nets.

**Consequence:** a device cannot connect directly across an assembly boundary;
both crossing endpoints must be connector contacts. Direct solder exits are
explicit `direct_pin` or `direct_wire` connectors. Reusable definitions and
deployed instances remain separate, and the firmware validates the complete
expanded topology before publishing it. The normative contract is
[Hardware configuration v2](../hwconfig-v2-model.md).

## Provider and consumer Resource manifests are separate

**Decision:** `<device>/manifest` continues to publish only Managed Resources.
Bound Remote Resources with valid sources are derived into the separate
versioned `<device>/manifest/consume` document.

**Reason:** what a node provides and what it depends on are different graph
directions and lifecycles. Remote declarations already contain all dependency
metadata, so requiring a second project declaration would create drift.

**Consequence:** bind, source changes, unbind, reconnect, and identity cleanup
also maintain the retained JSON and MessagePack consume manifests.

## dependsOn() means authoritative value mirroring

**Decision:** `a.dependsOn(b)` declares that `a` and `b` are the same logical
value with `b` authoritative. `ResourcesManager` copies every authoritative
update of `b` into `a` and publishes `a`. A Managed Value has at most one
dependency and the last call wins. The edge is published as a singular
`depends_on` naming `b`'s local Resource name.

**Reason:** the useful relationship between two Resources that represent one
fact is mirroring, not arbitrary causation. Expressing it as a general
dependency list would describe something the framework cannot act on, and would
leave the synchronization as project code the declaration was supposed to
replace. A Remote Resource already has a stable local identity whose source is
separately configurable, so naming the local Resource keeps provenance
(`<device>/manifest/consume`) and mirroring (`<device>/manifest`) as one join
rather than two competing address systems.

**Consequence:** mirroring uses the internal owner path, so a write handler
never sees it -- that handler means "someone is asking to change this", which a
source changing underneath it is not. Propagation is one level and scans the
registry rather than keeping a reverse index.

Withdrawal propagates with the value. When a source's retained state is
tombstoned, or it is retargeted, cleared or unbound, each dependent goes stale,
stops being authoritative and has its retained `/state` tombstoned. A mirror
whose source has no value must not keep advertising one, because its `/state`
is retained and a consumer that does not resolve `depends_on` cannot tell the
difference.

`ManagedSensor<T>` is the only dependent in version 1. `ManagedState<T>` is
excluded until write-through is designed: a `/set` on a dependent should travel
to the authoritative source and return as an ordinary mirrored update, and
until that exists nothing writable should advertise a value it cannot change.
Actions and Remote Values cannot declare a dependency at all.

`dependsOn()` asserts semantic identity, and only the application can know
whether two Resources mean the same thing. The firmware validates what it can
see -- no self-dependency, matching wire types -- and backend tooling is
expected to report unresolved names, type mismatches and unsupported chains.
Semantic correctness belongs to the application.

Chain resolution, cross-device source-of-truth discovery and automatic
subscription redirection are deliberately deferred; the manifest publishes only
the direct local edge.

## Status is presence, not Resource freshness

**Decision:** `<device>/status` contains the logical name, hardware signature, configured timezone, and online/offline presence.

**Reason:** device presence and individual Resource freshness are different facts. A device may be online while one Resource has never reported state, or retained Resource state may remain available after the device goes offline.

**Consequence:** consumers must not infer Resource freshness solely from `status.online`.

## Timezone belongs to device identity

**Decision:** the configured POSIX timezone is persisted and applied by `DeviceIdentity`, and is reported in `/status` and the `/info` identity section.

**Reason:** timezone is device-wide configuration that affects local-time presentation and wall-clock scheduling. Keeping it with identity gives commands, startup, status, and INFO one source of truth.

**Consequence:** changing timezone applies immediately and refreshes retained identity documents when MQTT is connected. Epoch timestamps remain UTC-based.

## One status JSON shape for online, offline, and Last Will

**Decision:** normal online publication, graceful offline publication, MQTT Last Will, and old-identity cleanup use the same status JSON shape.

**Reason:** observers should not need separate parsers depending on how a device disconnected.

**Consequence:** the earlier literal `"online"` / `"offline"` payload form is not supported as a compatibility mode.

## Scheduler commands and callbacks share one timing engine

**Decision:** command jobs and callback jobs use the same `Job` representation and scheduling logic.

**Reason:** time semantics and execution semantics are independent. Duplicating schedulers for each target would create two implementations of due time, repeat behavior, removal, and listing.

**Consequence:** every valid job must contain exactly one execution target: a command or a callback.

## Scheduler callbacks are plain function pointers

**Decision:** callbacks are `void (*)()`, which also supports non-capturing lambdas.

**Reason:** this keeps the runtime representation small and predictable on embedded targets and avoids introducing `std::function` solely to support captures.

**Consequence:** capturing lambdas are not supported by the current Scheduler API. Code needing state must use another ownership/context pattern at the application level.

## Only wall-clock command jobs persist

**Decision:** persisted jobs are limited to wall-clock String command jobs.

**Reason:** a callback pointer is only meaningful inside the current firmware/runtime, and a monotonic deadline is only meaningful inside the current boot.

**Consequence:** callback jobs, including wall-clock callbacks, disappear on reboot. Monotonic jobs also disappear on reboot.

## USER and MANAGED Scheduler scopes are separate

**Decision:** C++ framework/application jobs are `MANAGED`; jobs created through the JOB command surface are `USER`.

**Reason:** operator commands must not be able to accidentally delete framework work such as telemetry scheduling or identity-cleanup retry.

**Consequence:** `JOB LIST`, `JOB DELETE`, and `JOB CLEAR` only operate on USER jobs. Labels are unique within a scope rather than globally, so a USER label cannot block a MANAGED job with the same label.

## Scheduler may own a task or be manually ticked

**Decision:** one Scheduler supports both `TASK` and `MANUAL` execution modes.

**Reason:** some applications want complete cooperative control while others benefit from framework-owned scheduling.

**Consequence:** the timing engine is the same in both modes; only who calls `tick()` changes. Calling `tick()` manually while also choosing TASK mode is application misuse rather than a separate supported ownership model.

## Framework Scheduler jobs use callbacks, not command Strings

**Decision:** internal recurring work such as telemetry publication is scheduled as direct callbacks.

**Reason:** framework code already has a typed C++ API. Routing its own maintenance work back through the text command parser would add avoidable parsing and coupling.

**Consequence:** String commands remain useful for persisted/user-visible jobs, while internal framework work stays direct.

## Command and Resource Action remain separate concepts

**Decision:** operator/framework commands are not modeled as Resource Actions.

**Reason:** Resource Actions describe application capabilities. Commands operate the framework, configuration, diagnostics, and administrative control plane.

**Consequence:** there are two deliberate execution surfaces. A capability such as `learn_ir` remains a Resource Action, while `INFO NETWORK`, `TIME`, or `JOB CLEAR` is a command. The `>` Resource-command syntax is only an adapter from command transports into existing Resources; it does not create a second Resource model.

## Ordinary `/invoke` is fire-and-forget

**Decision:** the Resource `/invoke` topic does not carry a result channel or retained execution state.

**Reason:** most Action requests only need to be delivered. Adding request IDs and result topics to every invocation would make the basic Resource protocol heavier.

**Consequence:** successful raw `/invoke` publish means accepted for transport, not successful remote execution. A correlated command/MQTTP request using the `>` Resource-command form can surface the `ActionResult` of a ManagedAction executed on the receiving device. Invoking a RemoteAction still only reports whether its MQTT publication was accepted.

## Remote local identity is separate from its source

**Decision:** every Remote Resource has a stable local name while its remote
`OWNER/RESOURCE` source is configurable and persisted in
`/remoteresources.json`.

**Reason:** application code and commands need a durable local handle even when
the selected remote provider changes.

**Consequence:** `setSource()` changes routing, subscriptions, freshness, and
the persisted binding, but never renames the local Resource. The canonical
`SOURCE` Resource verb replaces project-specific source-selection Actions.

## Identity adoption is a migration, not a live rename

**Decision:** once the MQTT address is locked for a boot, adoption persists the new name but the running device continues using the current address until reboot.

**Reason:** changing the address live would require safely moving subscriptions, Last Will, Resource state, manifests, status, queued traffic, and project assumptions as one atomic operation.

**Consequence:** adoption may require reboot before the new name becomes active. Old retained state is cleaned afterward through explicit retryable migration state.

## Old identity cleanup only knows currently declared Resources

**Decision:** cleanup withdraws retained state for Managed Values that exist in the current firmware plus the old provider manifest, consume manifest, and status.

**Reason:** the framework has no historical registry of every Resource a previous firmware version might once have published.

**Consequence:** a Resource removed from firmware before identity cleanup may leave old retained state that current code cannot discover automatically.

## No backward-compatibility layer during the current architecture freeze

**Decision:** superseded internal/wire forms are removed instead of maintaining parallel aliases and migration paths.

Examples include the legacy Scheduler file import and literal status payloads.

**Reason:** NightMare is still converging on its first stable architecture. Carrying compatibility for pre-freeze designs would make the code and documentation permanently explain several models at once.

**Consequence:** users of older development snapshots may need to clear/recreate persisted state when moving to the frozen model.
