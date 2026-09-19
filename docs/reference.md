---
title: API reference
description: Public types and methods in the active library.
section: reference
order: 1
---

# API reference

Include `<NightMare.h>` or a specific header under `<NightMare/...>`. Public names are in the `NightMare` namespace. `NightMare::Network` should be qualified when Arduino's global `Network` object is also visible.

| Type | Main entry points |
| --- | --- |
| `NetResource` | `id()`, `kind()`, `type()`, `access()`, `metadata()` |
| `NetValue<T>` | `get()`, `set(value)`, `encode()`, `decodeAndSet(text)` |
| `NetAction<Args>` | `response()`, `parse(text, args)`, `encode(args)` for non-void Args |
| `NetEvent<Payload>` | `parse(text, payload)`, `encode(payload)` for non-void Payload |
| `ResourceManager` | `add`, `mirror`, `onWrite`, `onAction`, `onUpdate`, `onEvent`, `onResult`, `set`, `request`, `invoke`, `emit`, `invokeLocal`, `registry` |
| `Network` | `resources()`, `tick()`, `droppedMessages()` |
| `MqttTransport` | `attach(network)`, `begin(uri, user, password, certificate)`, `end()`, `connected()` |
| `Console` | `execute(line)`, `tick(stream)` |
| `Scheduler` | `after`, `every`, `dailyAt`, `cancel`, `tick` |
| `Runtime` | `add`, `scheduler()`, `tick()`, `startManaged`, `stopManaged` |
| `Time` | `now`, `valid`, `setEpoch`, `second`, `minute`, `hour`, `day`, `month`, `year` |

`PublishPolicy{onChange, periodMs}` is passed to `ResourceManager::add`. `ResourceMetadata` is caller-owned and optional. Its `fields` array describes ordered structured Action arguments. `NetCodec<T>` may be specialized for an application's struct. All runtime handler registrations use a function pointer and optional `void*` context stored in the Manager; Resources store no handlers.

The default capacities are 32 Resources, 16 Jobs, 8 inbound Network messages and 8 pending Action replies. `NIGHTMARE_MAX_RESOURCES` and `NIGHTMARE_MAX_JOBS` can be set at build time. The current Console input limit is 128 characters. Transport payloads should fit a single ESP MQTT data event.
