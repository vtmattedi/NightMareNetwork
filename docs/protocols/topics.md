---
title: MQTT topics
description: Canonical MQTT topics and retention rules used by NightMare Network.
section: protocols
order: 10
---

# MQTT topics

NightMare Network uses MQTT topics as a small wire protocol. Topic names are case-sensitive.

The current protocol is device-rooted:

```text
<device>/...
```

Namespace is not yet encoded into the topic address.

## Device-name rules

A NightMare device name is one MQTT topic segment.

It must:

- contain 1 to 64 characters,
- not be `all`,
- not contain `/`, `+`, or `#`,
- not contain control characters below `0x20`.

`all` is reserved for broadcast command input.

## Topic map

The current standard topic families are:

```text
<device>/status

<device>/info
<device>/hardware/msgpack
<device>/hardware/json
<device>/telemetry/system
<device>/telemetry/network
<device>/telemetry/heartbeat

<device>/manifest/msgpack
<device>/manifest/json
<device>/manifest/consume/msgpack
<device>/manifest/consume/json
<device>/resource/<name>/state
<device>/resource/<name>/set
<device>/resource/<name>/invoke

<device>/console/in
<device>/console/out
<device>/console/controlled/<id>/in
<device>/console/controlled/<id>/out

all/console/in

Control/request
Control/time
```

Not every device subscribes to every topic. Subscriptions are installed according to enabled features and bound Resources.

## Retained topics

NightMare uses retained messages for current state and current description.

| Topic | Retained | Purpose |
|---|---:|---|
| `<device>/status` | yes | name, hardware signature, timezone + online/offline presence |
| `<device>/info` | yes | boot-scoped/static device information |
| `<device>/hardware/msgpack` | yes | canonical positional hardware configuration |
| `<device>/hardware/json` | yes | optional readable hardware sibling |
| `<device>/telemetry/system` | yes | last published runtime system telemetry |
| `<device>/telemetry/network` | yes | last published network bookkeeping |
| `<device>/manifest/msgpack` | yes | compact positional Resource manifest |
| `<device>/manifest/json` | yes | optional readable Resource manifest |
| `<device>/manifest/consume/msgpack` | yes | compact consume manifest |
| `<device>/manifest/consume/json` | yes | optional readable consume manifest |
| `<device>/resource/<name>/state` | yes | authoritative Value state |

An empty retained payload is used as a tombstone where NightMare needs to remove retained state.

For Resource Value state specifically, an empty retained `/state` means the
state has been withdrawn. A bound Remote Value becomes unavailable; withdrawal
does not redefine freshness as `STALE`. Its last decoded value remains stored
as last-known data.

## Transient topics

Requests, commands, and command responses are not retained.

| Topic | Retained | Purpose |
|---|---:|---|
| `<device>/resource/<name>/set` | no | request a writable Value change |
| `<device>/resource/<name>/invoke` | no | invoke an Action |
| `<device>/console/in` | no | ordinary command request |
| `<device>/telemetry/heartbeat` | no | runtime-configurable device heartbeat |
| `<device>/console/out` | no | ordinary command response/status |
| `<device>/console/controlled/<id>/in` | no | correlated command request |
| `<device>/console/controlled/<id>/out` | no | correlated command response |
| `all/console/in` | no | broadcast command request |
| `Control/request` | no | global control request, currently time sync |
| `Control/time` | no | global time-sync response |

## Resource topics

### Manifest

```text
<device>/manifest/msgpack
<device>/manifest/json
```

The MessagePack manifest is canonical and retained. The JSON sibling is
published only when `NM_ENABLE_JSON_WIRE=1`. The bare `manifest` path is a
namespace, not a payload topic.

It is descriptive metadata. It does not make Value state fresh and does not gate `/state`, `/set`, or `/invoke`.

### Consume manifest

```text
<device>/manifest/consume/msgpack
<device>/manifest/consume/json
```

The canonical retained document represents every declared Remote Resource once.
An entry with `bound: true` derives an active consume edge; an unbound entry is
still discoverable and configurable. JSON is optional, and the bare consume
root is a namespace.

### Value state

```text
<device>/resource/<name>/state
```

Retained owner state for a Value.

The payload is the Value's codec representation, not a generic JSON envelope.

Examples:

```text
true
23
23.5
cool
```

### Value write request

```text
<device>/resource/<name>/set
```

Transient request to a Managed `READ_WRITE` Value.

The payload uses the same Value codec representation as `/state`.

### Action invocation

```text
<device>/resource/<name>/invoke
```

Transient Action request.

The payload is passed to the Action as its canonical payload String. Schema-based Actions normally use a JSON object payload, but payload validation is optional at compile time.

There is no Resource result topic.

See [Resource protocol](resources.md) for the complete Resource wire contract.

## Resource subscriptions

NightMare subscribes only where a bound Resource needs ingress.

| Bound Resource | Automatic ingress subscription |
|---|---|
| `ManagedSensor<T>` | none |
| `ManagedState<T>` | its own `/set` |
| `RemoteSensor<T>` | owner's `/state` |
| `RemoteState<T>` | owner's `/state` |
| `ManagedAction` | its own `/invoke` |
| `RemoteAction` | none |

When `NM_ENABLE_REMOTE_RESOURCE_VERIFICATION` is enabled, the manager
also subscribes once to:

```text
<remote-device>/manifest/msgpack
```

per remote owner. The compact manifest is decoded for compatibility diagnostics
against the local Remote-resource declarations. This verification subscription
is separate from optional JSON `+/manifest/json` discovery/handler subscriptions.

These subscriptions are rebuilt after MQTT reconnect.

## Ordinary console

A device with Console + MQTT enabled subscribes to:

```text
<device>/console/in
all/console/in
```

A command received on either path is executed by the normal NightMare command handler.

The response is published to:

```text
<device>/console/out
```

For `all/console/in`, every receiving device replies on its own `<device>/console/out`; broadcast input is therefore not a single correlated request/response channel.

On MQTT connection, NightMare also publishes a transient message on `<device>/console/out`:

```text
Booted
```

for the first connection in the boot, and:

```text
Connected
```

on later MQTT connections.

## Controlled console / MQTTP

A device also subscribes to:

```text
<device>/console/controlled/+/in
```

A request at:

```text
<device>/console/controlled/<id>/in
```

is answered at:

```text
<device>/console/controlled/<id>/out
```

The `<id>` is supplied by the caller and creates the correlation.

See [MQTTP](mqttp.md) for the exact ID rules and request/response behavior.

## Time synchronization topics

Automatic time synchronization now uses ESP32 SNTP. The MQTT control topics remain as an auxiliary timestamp path.

When time synchronization is enabled, NightMare subscribes to:

```text
Control/time
```

If MQTT connects while NightMare's wall clock is not valid, the device publishes:

```text
topic:   Control/request
payload: time
```

The request is transient and is not prefixed with the device name.

A `Control/time` payload must be a JSON object containing both:

```json
{
  "timestamp": 1790000000,
  "offset": -10800
}
```

`timestamp` is consumed by the current implementation. A value larger than the 32-bit Unix-seconds range is treated as milliseconds and divided by 1000.

`offset` is currently required for the message to be accepted but is not otherwise used by the router.

## Project transport subscriptions

Applications may register additional topic filters with:

```cpp
NightMare::Subscribe(...);
NightMare::Unsubscribe(...);
```

Subscriptions are remembered in RAM by `NmConnection` and restored once on
reconnect alongside framework and Resource subscriptions. Messages that are
not consumed by a framework route are delivered to the handler registered with
`NightMare::OnMessage()`.

The current limits are:

```text
all subscriptions:   256
topic filter length: 192 characters
```

NightMare validates MQTT wildcard placement for `+` and `#`.
