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
<device>/hardware
<device>/hardware/msgpack
<device>/telemetry/system
<device>/telemetry/network

<device>/manifest
<device>/manifest/msgpack
<device>/manifest/consume
<device>/manifest/consume/msgpack
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
| `<device>/hardware` | yes | readable hardware topology JSON |
| `<device>/hardware/msgpack` | yes | compact positional hardware topology |
| `<device>/telemetry/system` | yes | last published runtime system telemetry |
| `<device>/telemetry/network` | yes | last published network bookkeeping |
| `<device>/manifest` | yes | Resource manifest |
| `<device>/manifest/msgpack` | yes | compact positional Resource manifest |
| `<device>/manifest/consume` | yes | Remote Resources this device consumes |
| `<device>/manifest/consume/msgpack` | yes | compact consume manifest |
| `<device>/resource/<name>/state` | yes | authoritative Value state |

An empty retained payload is used as a tombstone where NightMare needs to remove retained state.

For Resource Value state specifically, an empty retained `/state` means the state has been withdrawn. A bound Remote Value becomes `STALE`, while its last known value remains readable.

## Transient topics

Requests, commands, and command responses are not retained.

| Topic | Retained | Purpose |
|---|---:|---|
| `<device>/resource/<name>/set` | no | request a writable Value change |
| `<device>/resource/<name>/invoke` | no | invoke an Action |
| `<device>/console/in` | no | ordinary command request |
| `<device>/console/out` | no | ordinary command response/status |
| `<device>/console/controlled/<id>/in` | no | correlated command request |
| `<device>/console/controlled/<id>/out` | no | correlated command response |
| `all/console/in` | no | broadcast command request |
| `Control/request` | no | global control request, currently time sync |
| `Control/time` | no | global time-sync response |

## Resource topics

### Manifest

```text
<device>/manifest
```

The manifest is retained and describes the Resources implemented by the device.

It is descriptive metadata. It does not make Value state fresh and does not gate `/state`, `/set`, or `/invoke`.

### Consume manifest

```text
<device>/manifest/consume
<device>/manifest/consume/msgpack
```

These retained documents describe the valid, bound Remote Resources consumed
by the device. They are generated from the Resource registry and remain
separate from the provider manifest.

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

For Remote Resources, the manager also subscribes once to:

```text
<remote-device>/manifest
```

per remote owner so it can receive descriptive manifest information.

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

## Discovery subscriptions

Global device discovery is opt-in through `MQTT_SetDiscovery(true)`.

When enabled, NightMare subscribes to:

```text
+/manifest
+/status
```

`+/manifest` feeds valid other-device manifests to the configured Resource manifest handler.

`+/status` is ordinary MQTT traffic; when the project message callback is configured to receive external topics, status messages can reach that callback.

Discovery is separate from Remote Resource binding. A bound Remote Resource installs the exact subscriptions it needs even when global discovery is disabled.

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

## Project MQTT subscriptions

Applications may register additional topic filters with:

```cpp
MQTT_SubscribeTopic(...);
MQTT_UnsubscribeTopic(...);
```

Custom subscriptions are remembered in RAM and restored on reconnect.

The current limits are:

```text
custom subscriptions: 16
topic filter length:   192 characters
```

NightMare validates MQTT wildcard placement for `+` and `#`.

Framework-owned subscriptions remain owned by the framework even if the same filter is also requested by application code.

## Project message callback

Automatic NightMare routing runs before the project callback.

The order is conceptually:

```text
incoming MQTT
    |
    +-> Resource routing
    +-> time sync
    +-> console / controlled console
    |
    `-> project MQTT callback, if still unconsumed
```

By default, `MQTT_onMessage()` only receives topics rooted at the current device and receives them with the `<device>/` prefix removed.

With `onlyDeviceMessages = false`, the callback receives unconsumed full MQTT topics.
