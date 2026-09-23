---
title: Resource protocol
description: MQTT wire format, manifests, Value state, writes, and Action invocation.
section: protocols
order: 20
---

# Resource protocol

Resources are the application-level contract between NightMare devices.

The current Resource protocol has four topic shapes:

```text
<device>/manifest
<device>/resource/<name>/state
<device>/resource/<name>/set
<device>/resource/<name>/invoke
```

The central rule is:

> **A manifest describes. `/state` tells the truth.**

## Protocol version

The current Resource manifest version is:

```text
2
```

The version appears in the retained manifest document.

## Resource names

A Resource name is one MQTT topic segment.

It must:

- contain 1 to 64 characters,
- not contain `/`, `+`, or `#`,
- not contain control characters below `0x20`.

A device can bind at most:

```text
100 Resources
```

A resolved `(device, resource-name)` address may only be represented once in the local Resource registry.

## Manifest

A device publishes its retained manifest at:

```text
<device>/manifest
```

Only Resources **Managed by that device** appear in its manifest. Remote Resources are local references and are not announced as capabilities of the current device.

A representative manifest is:

```json
{
  "version": 2,
  "resources": [
    {
      "name": "temperature",
      "kind": "value",
      "access": "read",
      "type": "float"
    },
    {
      "name": "power",
      "kind": "value",
      "access": "read_write",
      "type": "boolean"
    },
    {
      "name": "set_timer",
      "kind": "action",
      "arguments": [
        {
          "name": "seconds",
          "type": "integer",
          "required": true
        }
      ]
    }
  ]
}
```

The manifest is retained.

The current manifest payload limit is:

```text
16384 bytes
```

### Compact manifest

The same manifest is also retained as MessagePack at:

```text
<device>/manifest/msgpack
```

Its positional schema is:

```text
[encodingVersion, manifestVersion, resources[]]

value  = [0, name, accessEnum, typeEnum]
action = [1, name, arguments[]]
arg    = [name, typeEnum, required]
```

Encoding version `1` is current. Array positions and numeric enums are
append-only. Readers that do not recognize the encoding version use the JSON
manifest at `<device>/manifest`.

## Resource kinds

`kind` is one of:

```text
value
action
```

Values include `access` and `type`.

Actions include an `arguments` array.

## Value access

The manifest encodes Value access as:

```text
read
read_write
```

The standard C++ wrappers map to these policies:

```text
ManagedSensor<T>   -> read
RemoteSensor<T>    -> expects a readable remote Value

ManagedState<T>    -> read_write
RemoteState<T>     -> expects a writable remote Value
```

A locally declared read-only Remote Value may observe a remote `read_write` Value. A local `read_write` Remote declaration requires the remote manifest to declare `read_write` for compatibility diagnostics.

Manifest compatibility is diagnostic only and does not gate state or requests.

## Value types

The wire taxonomy is:

```text
string
boolean
integer
float
struct
```

The built-in `NetCodec<T>` implementations currently cover:

```text
String
bool
integral C++ types
floating-point C++ types
```

`struct` exists in the wire taxonomy and Action argument metadata, but there is no generic built-in `NetCodec<T>` that silently serializes arbitrary C++ structs.

Unsupported Value C++ types fail at the codec boundary rather than degrading to String.

## Value state

A Managed Value publishes its state at:

```text
<device>/resource/<name>/state
```

The state is retained.

The payload is the encoded Value itself; it is not wrapped in a generic JSON object.

### Boolean

Encoding:

```text
true
false
```

Decoding also accepts:

```text
1
0
```

and case-insensitive `true` / `false`.

### Integer

Encoding is decimal text.

Examples:

```text
0
42
-12
```

Decoding rejects trailing text and rejects values outside the C++ target type's range.

Unsigned types reject negative input.

### Floating point

Encoding uses a compact decimal representation that round-trips to the same `float` or `double`.

Examples may look like:

```text
23.5
0.125
1.25e+06
```

Decoding uses the whole payload as one floating-point number; trailing text is rejected.

### String

The payload is the raw String.

Example:

```text
cool
```

There is no JSON quoting added by the Resource protocol.

An empty String is not a valid Resource String value because the empty MQTT payload is reserved as the retained deletion marker.

## State freshness

Remote Value freshness is driven by `/state`.

A valid non-empty owner state makes the Remote Value:

```text
FRESH
```

An empty `/state` payload means the retained state was deleted and makes the Remote Value:

```text
STALE
```

The last known decoded value remains readable when a tombstone is received.

Changing a Remote Resource's source is different: it resets the old source state entirely, including authoritative value presence, freshness, optimistic state, and update timestamps.

Before the first owner state is learned, freshness is:

```text
UNKNOWN
```

## Manifest does not control freshness

The manifest has a different lifecycle from Value state.

The following do **not** change Value freshness:

- a missing manifest,
- a withdrawn manifest,
- a malformed manifest,
- a manifest that does not contain the expected Resource,
- a manifest type/access disagreement.

Likewise, manifest compatibility does not gate:

```text
/state
/set
/invoke
```

This separation lets state remain useful even when descriptive metadata is absent or temporarily inconsistent.

## Remote manifest diagnostics

When a manifest from a device used by local Remote Resources arrives, NightMare compares it with the local declarations.

For Values it checks:

- Resource exists,
- `kind` matches,
- `type` matches,
- if the local Remote Value is `read_write`, remote `access` is also `read_write`.

For Actions it checks that the Action exists and, when the local RemoteAction declares expected arguments, compares those expectations with the remote schema.

Disagreements are logged. They do not rewrite the local declaration and do not disable traffic.

## Value write request

A writable Value receives requests at:

```text
<device>/resource/<name>/set
```

The message is transient.

The payload uses the same codec as `/state`.

Example:

```text
topic:
bedroom-ac/resource/target_temperature/set

payload:
23.5
```

The implementing device only subscribes to `/set` for Managed Values declared `read_write`.

A `ManagedState<T>` decodes the payload and calls its `onWrite` handler.

The handler returns:

```text
true
    request accepted

false
    request rejected
```

If accepted, the requested Value becomes authoritative owner state and is published retained on `/state`.

If rejected or undecodable, state remains unchanged.

## Local Managed Value writes

Calling `setValue()` on a Managed Value changes local truth even when transport is unavailable.

NightMare attempts to publish the new retained `/state` best-effort. A later reconnect re-announces Managed Values that have authoritative state.

This reflects the ownership rule:

> Network availability does not decide whether the owner itself knows its own state.

## RemoteState writes

Calling `setValue()` on a RemoteState publishes:

```text
<owner>/resource/<name>/set
```

The call only reports success if the request was accepted by the transport.

Only after successful dispatch does the optimistic window begin.

During the optimistic window:

```text
getValue()
    returns the locally requested value
```

while:

```text
authoritativeValue()
    returns the owner's last reported state
```

Owner `/state` remains authoritative. Optimism is a temporary presentation rule, not ownership transfer.

The default optimistic window is:

```text
5000 ms
```

## Action manifest schema

Actions publish runtime argument metadata.

Each argument contains:

```json
{
  "name": "seconds",
  "type": "integer",
  "required": true
}
```

Argument names follow the same one-segment naming rules as Resources and must be unique inside the Action schema.

The schema is self-description first. It is published whether or not runtime payload assertion is enabled.

## Action invocation

An Action is invoked at:

```text
<device>/resource/<name>/invoke
```

The message is transient.

A RemoteAction can use:

```cpp
action.invoke(payload);
```

A successful return means the publish was accepted by the transport. It does **not** prove the remote Action executed or succeeded.

There is deliberately no standard:

```text
/result
/response
request-id
retained action execution state
```

in the basic Resource protocol.

## Action payload

The maximum Action payload is:

```text
2048 bytes
```

An empty Action payload is valid.

This differs from Value state, where empty means tombstone/deletion.

### With payload assertion disabled

The default is:

```cpp
NM_ENABLE_ACTION_PAYLOAD_ASSERTION 0
```

The canonical payload String is passed to the ManagedAction handler without generic schema validation.

### With payload assertion enabled

When:

```cpp
NM_ENABLE_ACTION_PAYLOAD_ASSERTION 1
```

schema-aware Action payloads are checked as JSON objects.

The checker:

- requires all arguments marked `required`,
- checks known argument types,
- allows optional arguments to be absent,
- allows unknown extra fields,
- treats an empty payload as an object with no fields.

That tolerance is intentional: new optional fields can be added without forcing older peers to reject a request.

For a zero-argument Managed Action, the accepted asserted payload forms are an empty payload or a JSON object.

## Action argument type checks

With payload assertion enabled:

```text
boolean
    JSON boolean

integer
    JSON integer

float
    JSON number, including integer JSON values

string
    JSON string

struct
    JSON object or array
```

## ManagedAction result

A ManagedAction returns:

```cpp
struct ActionResult
{
    bool success;
    String result;
};
```

Raw MQTT `/invoke` has nowhere to return that result, so the result is consumed locally and only failures are logged.

Code using `ResourcesManager::executeAction()` directly can preserve the `ActionResult`.

The current controlled-console/MQTTP transport is a command request/response mechanism; it is not an automatic Resource `/invoke` result channel. See [MQTTP](mqttp.md).

## Resource binding and subscriptions

Binding a Managed Resource:

- validates the local device identity and Resource name,
- locks the device address,
- adds it to the registry,
- publishes the updated manifest,
- publishes state immediately if a Managed Value already has authoritative state,
- subscribes to `/set` or `/invoke` when required.

Binding a configured Remote Resource:

- validates the remote device and Resource address,
- rejects a source pointing at the current device,
- prevents duplicate local representations of the same remote address,
- subscribes to the remote manifest,
- subscribes to owner `/state` for Remote Values.

A Remote Resource may also be bound before a source is configured. It participates in the local registry but has no network address or subscriptions until `setSource()` supplies a valid source.

## Retargeting a Remote Resource

`setSource(device, resource)` keeps the Resource role Remote and replaces only its target.

NightMare:

- unsubscribes old ingress,
- drops state learned from the old source,
- updates manifest subscription ownership,
- validates the new address,
- subscribes to the new ingress.

If the new source is invalid, names the current device, or collides with another bound Resource, the source is refused and the Remote Resource is left detached so it can be pointed somewhere valid later.

## Unbinding

Unbinding a Managed Value removes its retained `/state` when that Value had authoritative state, then republishes the Managed Resource manifest without it.

Unbinding a Managed Action only requires the manifest update because `/invoke` is not retained.

Remote unbinding removes subscriptions that are no longer needed.

## Reconnect

After MQTT reconnect, NightMare rebuilds exact Resource subscriptions and re-announces:

- the retained Resource manifest,
- every Managed Value that has authoritative state.

Applications do not need to manually republish all bound Resources after reconnect.

## Identity cleanup

When a device identity is migrated, Resource cleanup under the old device name publishes tombstones for:

- the old retained state of every **currently declared Managed Value**,
- the old manifest.

Actions need no separate cleanup because `/invoke` is transient.

A Value removed from firmware before cleanup cannot be discovered from the current Resource registry and therefore cannot be automatically tombstoned under the old identity.

## Limits

Current Resource protocol/runtime limits include:

```text
bound Resources per device: 100
Resource segment length:     64 characters
Value/Action payload:        2048 bytes
manifest JSON capacity:      16384 bytes
```
