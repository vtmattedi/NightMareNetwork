---
title: Editing the library
description: Architectural rules and maintenance guidance for humans and AI agents modifying NightMare Network.
section: contributing
order: 10
---

# Editing the library

This page is for maintainers and AI coding agents modifying NightMare Network itself.

It is not an end-user usage guide. Before changing the library, understand the current architecture and preserve its intentional boundaries.

## Source of truth

Use these repository areas as follows:

```text
src/
    authoritative current implementation

docs/
    authoritative documentation for the current architecture

examples/
    supported examples using the current API

Legacy/
    previous architecture kept for historical and migration reference
```

`Legacy/` is not part of the active library build and must not be used as a model for new code unless a task explicitly concerns legacy behavior or migration.

If documentation and active source disagree, `src/` determines current behavior. The documentation should then be corrected.

## Read before changing architecture

At minimum, read:

- [Core concepts](../concepts.md)
- [Responsibilities](../responsibilities.md)
- [Architecture](../architecture.md)
- [Design decisions](../architecture/design-decisions.md)
- [Known gaps](../architecture/known-gaps.md)

Do not introduce a new abstraction merely because a similar implementation exists in `Legacy/`.

## Architecture stability

The current Resource architecture is intentional.

Avoid opportunistic public API changes, compatibility shims for superseded pre-freeze designs, or parallel abstractions unless the task explicitly requires them.

An internal refactor should preserve observable behavior unless it is intentionally an API or protocol change.

## Core invariants

### Resource ownership is permanent

A Resource is either Managed or Remote for its lifetime.

Retargeting a Remote Resource changes its source. It does not change its ownership role.

### A manifest describes; `/state` tells the truth

The Resource manifest is descriptive metadata.

For Values, authoritative runtime state comes from:

```text
<device>/resources/<name>/state
```

Do not make Value freshness depend on receiving a manifest first.

### Device presence is not Resource freshness

`<device>/status` describes device presence.

It does not determine whether an individual Resource Value is fresh.

### Application state belongs in Resources

Sensor, actuator, and application state should normally be represented as Resources.

Do not duplicate application truth into framework telemetry.

Telemetry is framework/device bookkeeping.

### Commands and Resource Actions are separate

Commands belong to the framework/operator control surface.

Resource Actions belong to the application capability model.

The `>` Resource command syntax is an adapter into already-bound Resources. It does not create a second Resource model.

### RemoteSensor is strict; RemoteState is optimistic

A `RemoteSensor` exposes authoritative owner state directly.

A `RemoteState` may temporarily expose a local optimistic value after a successful write request.

The remote owner remains authoritative.

### USER and MANAGED jobs are separate

Scheduler jobs created by framework/application C++ code are `MANAGED`.

Jobs created through the operator `JOB` command surface are `USER`.

Operator operations must not accidentally remove framework/application jobs.

### Runtime-only jobs remain runtime-only

Callback jobs are not persisted.

Monotonic jobs are not persisted.

Only supported wall-clock String command jobs survive reboot.

### Cluster and namespace are different

A cluster is a Local MQTT topology boundary.

Namespace is a logical addressing/isolation concept.

Namespace is not currently encoded in the MQTT topic root.

### MQTT publication is not proof of remote execution

Successful MQTT publication means the local transport accepted the message.

It is not proof that another device executed an Action or applied a requested state.

Use a correlated request/response path when an execution result is required.

## Framework vs application responsibility

A useful ownership boundary is:

```text
Application
    hardware drivers
    sensor acquisition
    actuator implementation
    device-specific services
    business rules
    Resource declarations

NightMare
    identity
    Resource registration and routing
    canonical MQTT conventions
    reconnect behavior
    Scheduler infrastructure
    framework/operator commands
    device information and telemetry
    common ESP lifecycle
```

A practical test is:

> If unrelated device projects would otherwise reimplement the same network/runtime plumbing, it probably belongs in NightMare. If it expresses what one particular device actually does, it probably belongs in the application.

## Resource Manager and registry

`ResourcesManager` is the runtime boundary between declared Resources and NightMare transport/routing.

Applications create Resource objects and bind them to the manager.

The manager coordinates framework behavior around those Resources, including registration, manifests, subscriptions, routing, reconnect behavior, and remote-source changes.

It does not own the Resource objects themselves.

Do not introduce a second Resource registry or application-specific parallel routing model without an explicit architectural reason.

## Before changing a subsystem

Use the relevant conceptual, module, and protocol documentation together.

```text
Resources
    docs/concepts.md
    docs/modules/resources.md
    docs/protocols/resources.md

MQTT / networking
    docs/modules/network.md
    docs/protocols/topics.md
    docs/protocols/mqttp.md

Scheduler
    docs/modules/scheduler.md
    docs/architecture/design-decisions.md

Identity
    docs/modules/identity.md
    docs/protocols/status-info.md

Commands
    docs/protocols/commands.md

Telemetry
    docs/modules/telemetry.md
    docs/protocols/status-info.md

Time
    docs/modules/time.md

Platform lifecycle
    docs/modules/platform.md
```

## Feature and dependency rules

Feature flags and dependency checks in `NightMareConfig.h` / `Features.h` are part of the supported composition model.

Do not remove or weaken a dependency check merely to make one local build pass.

When changing a module, check whether the change affects:

```text
compile-time feature dependencies
umbrella includes
global services
startup order
tick ownership
optional ESP32 services
```

## Changing wire behavior

Changes to any of the following are protocol changes, not ordinary internal refactors:

```text
MQTT topic names
retained vs transient publication
JSON document shape
tombstone behavior
Resource manifest fields
Value encoding
/set semantics
/invoke semantics
status or telemetry lifecycle
correlated request/response behavior
```

When changing wire behavior, inspect and update at least:

- the relevant protocol documentation,
- module documentation,
- examples,
- design decisions or known gaps when the architectural contract changes,
- MCP-visible documentation,
- tests covering the affected behavior.

Avoid introducing a second compatibility representation unless explicitly required.

## Changing the public API

When modifying a public type, function, macro, global, or lifecycle entry point, check:

```text
public headers
umbrella headers
examples
docs/getting-started.md
docs/reference.md
relevant module docs
source comments
MCP get_api discoverability
```

A private implementation cleanup should not accidentally become an API change.

## Documentation rules

The documentation has separate layers.

Conceptual documentation explains what a concept means, why it exists, and how it relates to the architecture.

Module documentation explains how to use and reason about one implementation area.

Protocol documentation defines wire behavior and semantics.

`docs/reference.md` documents exact current public declarations and limits.

Historical behavior should be explicitly identified as historical.

Do not let obsolete concepts outrank the current architecture in documentation, examples, website navigation, or MCP search.

## Common agent mistakes

Avoid:

- inferring current behavior from `Legacy/`,
- recreating an older abstraction because its code already exists,
- introducing a second Resource registry,
- making device status determine Resource freshness,
- moving application state into telemetry,
- conflating Commands with Resource Actions,
- treating a Remote Resource source change as ownership mutation,
- routing framework maintenance through command Strings when a direct typed API exists,
- treating MQTT publication success as proof of remote execution,
- implementing a deferred Known Gap without a concrete requirement,
- adding speculative generic abstractions,
- silently changing retained MQTT semantics,
- updating source without updating documentation and examples.

## Validation before considering a change complete

At minimum:

```text
1. Build the affected library configuration.
2. Build relevant examples.
3. Run relevant tests.
4. Check feature combinations touched by the change.
5. Verify public examples still use the current API.
6. Update documentation for changed behavior.
7. Search docs/examples for obsolete identifiers or concepts.
8. Confirm protocol behavior did not change unintentionally.
```

For architectural changes, also revisit:

- `docs/architecture/design-decisions.md`
- `docs/architecture/known-gaps.md`

## Keep the model small

NightMare should grow from demonstrated reusable needs rather than speculative abstractions.

The existence of a possible future extension is not itself a reason to add another layer today.

When in doubt, preserve the smallest model that satisfies the current architecture and documented use cases.
