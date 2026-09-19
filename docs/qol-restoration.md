---
title: Device infrastructure and Console
description: Settings, identity, operator commands and standard ESP32 wiring.
section: architecture
order: 3
---

# Device infrastructure and Console

## Storage and identity

`SettingsStore` is a generic fixed-capacity key/value store with typed bool, signed and unsigned 32-bit integer, float and String accessors. `StorageMode::Memory` holds runtime flags; `StorageMode::Persistent` delegates persistence to a `SettingsPersistence` backend. `Esp32SettingsPersistence` stores `/configs.json` in LittleFS. Call `begin()` before use. `set`, `remove` and `clear` persist immediately. There are no config change callbacks; restart the device after changing settings consumed during startup.

Keys beginning with `_` are internal by library policy. `SettingsAccess::User` cannot read, write, remove or list them; `clear(User)` preserves them. `SettingsAccess::Internal` can access all keys. This is a caller-selected access policy, not authentication. A textual `--admin` argument never changes access. The default Console context uses `User`.

`DeviceIdentity` persists `_device_id` and keeps its stable protocol ID separate from `_device_label`. A missing ID becomes `esp32-nm-<hardware suffix>`. `adoptId()` stores a new ID for the next boot; the active Network retains its current ID until then. The standard facade uses the ID for WiFi hostname, OTA hostname, Resource owner and MQTT identity/topics.

## Command grammar

`CommandParser` supports repeated whitespace, double or single quoted arguments, escaped characters and empty quoted arguments. Input is limited to 256 characters and 12 words. `CommandRouter` returns a `CommandResult` with success and reply text. Applications can register up to eight custom operator command families with `registerCommand(name, handler, context)`.

| Command | Meaning |
| --- | --- |
| `> action [args]` | Invoke a local registered Action. A bare Action ID also works. |
| `> owner/action [args]` | Invoke a registered remote Action. The immediate reply says whether it was sent; its Action result uses the normal ResourceManager callback. |
| `< owner` | List locally registered Values owned by `owner`, including mirrored Values. `local` aliases this device. |
| `< owner value [maxAgeMs]` | Show the last known Value and remote freshness. The default max age is 30 seconds. |
| `resources [list]` | List registered Values, Actions and Events. |
| `config list|get|set|flag|exists|remove|clear` | Inspect or change user settings. |
| `system info|boot|status|restart` | Read direct ESP32 information or restart. `boot_info` and `hardware_info` are aliases. |
| `network status` | Show current connection and dropped inbound messages. |
| `device label [text]` / `device adopt id` | Manage display label or next-boot protocol ID. |

Scalar Action arguments are validated and encoded using their `NetValueType`; a String argument uses the protocol's `~` marker. Structured Actions use ordered `ResourceMetadata::Field` entries to turn positional arguments into the JSON array expected by the application's `NetCodec`. Console does not implement a separate Action protocol. The `<` command reads the ResourceManager's Value cache; it does not create a `VALUE_READ` MQTT operation. For remote freshness, the owner should publish unchanged state periodically.

`Console` is an ingress adapter for serial, MQTT or application calls. MQTT transport subscribes to `<device-id>/console/in` after every connection when Console is attached. Network dispatches the command on the Runtime thread and publishes result or error text to `<device-id>/console/out`. The direct NM-NW Action invoke/result path remains separate. There was no active `MQTTP` facility to port; current Action correlation and `Control/request`/`Control/time` cover the request/reply and clock use cases found in Legacy.

## Platform information and telemetry

`Esp32SystemInfo::hardware()`, `boot()` and `runtime()` return ordinary structs without a Network or ResourceManager. `hardwareText()`, `bootText()` and `statusText()` provide operator answers. Boot count uses RTC memory for resets where that memory survives. `TelemetryService` registers ordinary Values for uptime, heap, chip model, flash size, boot count, firmware version and time synchronization; with WiFi enabled it also exposes connection state, RSSI and IP address. Runtime's Scheduler updates dynamic Values; ResourceManager handles state publication and reconnect replay. `Esp32Device` enables telemetry by default. Advanced devices can instantiate the provider or service directly.

## Feature flags

The umbrella header and standard facade use `NIGHTMARE_ENABLE_SETTINGS`, `NIGHTMARE_ENABLE_CONSOLE`, `NIGHTMARE_ENABLE_TELEMETRY`, `NIGHTMARE_ENABLE_OTA`, `NIGHTMARE_ENABLE_WIFI` and `NIGHTMARE_ENABLE_MQTT` (all default to 1). Set individual macros to 0 in build flags for a smaller standard profile. Core Resource, Network, Runtime and memory SettingsStore headers remain usable for manual composition; enable an optional module's flag when using that module directly. `examples/basic` has an explicit `esp32c3-minimal` build environment with every optional layer disabled.

The GPIO light helper is `GpioLightService` under Platform; include its header explicitly. It is an example of application behavior, not standard device infrastructure.

## Legacy capability audit

| Legacy capability | Active treatment |
| --- | --- |
| Config and SystemSettings | Replaced differently by persistent and memory `SettingsStore` instances. |
| NightMareCommand and context/source | Replaced differently by bounded parser, router, source context, Console adapters and Action fallback. The old async command worker is deferred. |
| SystemStatus, BootInfo and hardware helpers | Restored through `Esp32SystemInfo`, operator commands and optional telemetry Resources. |
| MQTT local/remote broker switching | Restored as pollable two-broker failover in `MqttTransport`. |
| MQTT console and status | Restored `console/in` and `console/out`; retained online/offline status and reconnect subscriptions stay in transport. |
| Time synchronization | Existing `Control/request` and `Control/time` flow retained. |
| ServerVariable | Replaced by typed Values, Actions, Events and ResourceManager; no parallel variable protocol. |
| Old Scheduler/persistent string jobs | New typed Scheduler retained; persistent string commands intentionally deferred. |
| TCP server/client and controller proxies | Legacy only; no active port. |
| Old GPIO light controller | Moved to explicit platform helper `GpioLightService`. |

The website and MCP material are updated separately after the library transformation.
