---
title: Resources
description: Values, Actions, Events, registration, authority and metadata.
section: resources
order: 1
---

# Resources

Every network-visible capability is a Resource. IDs such as `temperature` are stable machine identities. Optional `ResourceMetadata` supplies display labels, units, bounds, grouping and ordered structured argument fields without adding several Strings to every Resource.

Resource and remote owner IDs are 1–64 characters using ASCII letters, digits, `_`, `-` or `.`. This keeps topic routing and Console invocation unambiguous.

## Values

```cpp
NetValue<float> temperature("temperature");
NetValue<uint8_t> brightness("brightness", NetAccess::READ_WRITE);
resources.add(temperature, {true, 60000}); // on change and every 60 s
resources.add(brightness);
resources.set(temperature, 23.5f);
```

`NetValue<T>` owns its value. Read-only is the default. `set` on the manager is the authoritative publication path; `value.set` only changes the local object. Frequent state messages contain the encoded scalar, with type and access already described by the schema. Supported scalar types are bool, fixed-width signed and unsigned integers, float, double and Arduino `String`.

New Values have no authoritative state until first set; discovery still announces them. A String state uses a leading `~` byte on the wire so an empty String (`~`) remains distinct from MQTT's zero-length retained-message deletion.

A remote mirror uses the same class:

```cpp
NetValue<float> otherTemperature("temperature");
resources.mirror(otherTemperature, "other-device");
resources.onUpdate(otherTemperature, onTemperature, context);
```

The remote owner's ID passed to `mirror` is caller-owned and must outlive the registration. String literals are a simple choice.

A remote state update changes `otherTemperature` without echoing it back. `resources.request(writableMirror, desired)` sends a write request to the owner; the mirror waits for an authoritative state update.

### Remote Value freshness

The Value holds the last known state. Freshness tells a consumer how recently this runtime received a valid remote `VALUE_STATE`:

```cpp
if (resources.freshness(otherTemperature, 30000) == ResourceFreshness::FRESH) {
    use(otherTemperature.get());
}
```

`UNKNOWN` means no valid remote state has ever arrived, `FRESH` means the last one arrived within the requested age, and `STALE` means it arrived earlier. The last known value remains available when stale. Every valid state refreshes the reception time, including a repeat of the same value. `ageMs(value)` returns the elapsed monotonic milliseconds, or `UINT32_MAX` if there is no received remote Value state; local Values, Actions and Events have `UNKNOWN` freshness. These queries do not scan resources or publish messages.

Freshness is local runtime knowledge and is not transmitted over NM-NW. It is meaningful when the owner has an expected update cadence. A Value published only on change can legitimately stay unchanged for a long time. For example, publish temperature on change with a 10-second periodic heartbeat and let consumers consider it stale after 30 seconds. Explicit resource availability is deferred.

## Actions

```cpp
NetAction<void> restart("restart", ActionResponse::ACK);
resources.add(restart);
resources.onAction(restart, handler, context);
resources.invoke(restart);
```

The manager calls `ActionHandler(context, resource, payload, result)` and expects an `ActionStatus`. With `NONE`, an invocation has no request ID or reply. `ACK` adds a compact request ID and returns a status. `RESULT` also returns the handler's result text. A pending reply expires after 10 seconds. For longer work, acknowledge the start and later emit an Event.

For a scalar argument, use `NetAction<uint8_t>` and `action.parse(payload, value)` in the handler. For structured arguments, define a struct and specialize `NetCodec<YourStruct>` with `encode` and `decode`. Add ordered `ResourceMetadata::Field` entries so discovery exposes the field IDs and types. The wire representation is a JSON array in schema field order, such as `[255,120,0]` for red, green and blue. The application's codec converts between that array and its struct.

## Events

```cpp
NetEvent<void> buttonPressed("buttonPressed");
resources.add(buttonPressed);
resources.emit(buttonPressed);
```

Events are not retained and have no current value. `NetEvent<T>` can encode a typed payload. A remote Event can be observed with `resources.onEvent(event, handler, context)`. If consumers must learn a condition after reconnect, expose a Value as well.

## Publication policy and metadata

`PublishPolicy{onChange, periodMs}` is stored in the registry. A `periodMs` of zero disables periodic publication. A local Value's registry entry stores its last publication time; a remote Value uses the same timestamp slot for its last valid state reception. No per-resource FreeRTOS task or timer is created.

The registry is fixed at `NIGHTMARE_MAX_RESOURCES` entries (32 by default). Override that macro at build time to fit the device's RAM budget. A Resource has only its ID String, enums, virtual table pointer and optional metadata pointer; metadata storage belongs to the caller.
