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
| `ResourceManager` | `add`, `mirror`, callbacks, `set`, `request`, `invoke`, `emit`, `invokeLocalResult`, `writeLocal`, `ageMs`, `freshness`, `registry` |
| `Network` | `resources()`, `attachConsole()`, `tick()`, `connected()`, `droppedMessages()` |
| `MqttTransport` | `attach`, `begin`, `configureFailover`, `tick`, `usingBackup`, `end`, `connected` |
| `SettingsStore` | `begin`, typed `get`/`set`, `exists`, `remove`, `clear`, `visit`, `count` |
| `DeviceIdentity` | `begin`, `id`, `label`, `setLabel`, `adoptId` |
| `CommandParser` / `CommandRouter` | `parse`, `execute`, `registerCommand`, `attachSettings`, `attachIdentity`, `attachNetwork` |
| `Console` | `run(line, context)`, `execute(line)`, `tick(stream)` |
| `Esp32SystemInfo` | `hardware`, `boot`, `runtime`, text helpers |
| `TelemetryService` | `begin(resources, runtime, firmwareVersion)`, `update` |
| `Esp32Device` | `begin(options)`, `tick`, accessors for common components |
| `Scheduler` | `after`, `every`, `dailyAt`, `cancel`, `tick` |
| `Runtime` | `add`, `scheduler()`, `tick()`, `startManaged`, `stopManaged` |
| `Time` | `now`, `valid`, `setEpoch`, `second`, `minute`, `hour`, `day`, `month`, `year` |

`PublishPolicy{onChange, periodMs}` is passed to `ResourceManager::add`. `ResourceMetadata` is caller-owned and optional. Its `fields` array describes ordered structured Action arguments. `NetCodec<T>` may be specialized for an application's struct. All runtime handler registrations use a function pointer and optional `void*` context stored in the Manager; Resources store no handlers.

The default capacities are 32 Resources, 48 Settings, 16 Jobs, 8 inbound Network messages, 8 pending Action replies and 8 custom operator command families. `NIGHTMARE_MAX_RESOURCES`, `NIGHTMARE_MAX_SETTINGS` and `NIGHTMARE_MAX_JOBS` can be set at build time. Console input is limited to 256 characters and 12 words. Transport payloads should fit a single ESP MQTT data event. See [device infrastructure and Console](qol-restoration.md) for command grammar and feature flags.
