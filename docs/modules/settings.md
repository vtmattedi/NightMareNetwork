---
title: Settings and runtime state
description: In-memory RuntimeState and persistence-backed StateStore behavior.
section: modules
order: 60
---

# Settings and runtime state

NightMare has two generic String key/value types and one typed runtime facility:

```text
RuntimeState
    RAM only

StateStore
    RuntimeState behavior + optional persistence

SystemStateStore
    allocation-free framework facts and pending requests
```

The standard global instances are:

```cpp
SystemState
PersistentSettings
```

`SystemState` is not a key/value store. `PersistentSettings` remains a
`StateStore` backed by the existing String model.

## RuntimeState

`RuntimeState` is a small in-memory String key/value store.

```cpp
RuntimeState state;
```

It never accesses filesystem storage.

Current capacity:

```cpp
RuntimeState::MaxEntries == 64
```

## Set

```cpp
state.set("mode", "cool");
```

A key must be non-empty.

Setting an existing key replaces its value.

Adding a new key fails when the store already contains 64 entries.

## Get

```cpp
String mode = state.get("mode", "off");
```

The default is returned when the key does not exist.

## Flags

Convenience helpers store booleans as:

```text
"1"
"0"
```

Use:

```cpp
state.setFlag("ready", true);

if (state.getFlag("ready"))
{
    // ...
}
```

## Exists and remove

```cpp
state.exists("mode");
state.remove("mode");
```

`remove()` returns false when the key does not exist.

## Clear

```cpp
state.clear();
```

removes all entries.

## JSON

```cpp
String json = state.toJson();
```

Values are serialized as JSON Strings.

For example:

```json
{
  "mode": "cool",
  "ready": "1"
}
```

`RuntimeState` is not a typed JSON object store. Its stored values are Strings.

## SystemState

The global:

```cpp
SystemState
```

is an allocation-free `SystemStateStore`. Runtime facts use typed flags:

```cpp
if (!SystemState.get(SystemFlag::OtaRunning))
    readSensor();

SystemState.set(SystemFlag::TimeSynced);
SystemState.clear(SystemFlag::TimeSynced);
```

Pending framework work is kept in a separate typed bank:

```cpp
SystemState.request(SystemRequest::PublishHardwareJson);

if (SystemState.take(SystemRequest::PublishHardwareJson))
{
    if (!publishHardwareJson())
        SystemState.request(SystemRequest::PublishHardwareJson);
}
```

`pending()` observes a request without clearing it. `take()` atomically tests
and clears one. Operations are task-safe, not ISR-safe. Disabled modules leave
their flags false. The storage size is derived from the enum `Count` members;
the API does not expose raw masks.

## StateStore

`StateStore` derives from `RuntimeState`.

```cpp
StateStore store(true);
```

The constructor parameter selects whether that instance is persistent.

The global:

```cpp
PersistentSettings
```

is constructed as persistent.

## Persistent file

The current persistent file is:

```text
/configs.json
```

This path is an implementation detail, not an application protocol.

## Begin

```cpp
PersistentSettings.begin();
```

For a persistent store, `begin()`:

1. mounts LittleFS with `LittleFS.begin(true)`,
2. updates `SystemFlag::PersistentStorageReady`,
3. loads `/configs.json`,
4. creates/saves an empty settings file if none exists.

Initialization is idempotent.

## Invalid settings file

If the file exists but cannot be parsed as the expected JSON object, `begin()` clears the in-memory RuntimeState view.

The invalid file is left untouched until settings are later written.

This avoids silently overwriting the only copy merely because loading failed.

## Set and automatic save

For a persistent StateStore:

```cpp
PersistentSettings.set("name", "value");
```

performs the RuntimeState update and immediately calls `save()`.

The returned `bool` reflects both the in-memory operation and the save.

For a nonpersistent StateStore, `set()` only changes RAM.

## Get

```cpp
PersistentSettings.get("name", "default");
```

ensures the store has been initialized first.

If initialization fails, the supplied default is returned.

## Exists

```cpp
PersistentSettings.exists("name");
```

also initializes first.

Initialization failure makes the result false.

## Remove

```cpp
PersistentSettings.remove("name");
```

removes the key and automatically saves when persistent.

## Clear

Normal clear:

```cpp
PersistentSettings.clear();
```

clears RAM and saves the empty store.

There is also:

```cpp
PersistentSettings.clear(false);
```

which clears the in-memory copy without immediately saving.

This is an implementation-level escape hatch; normal application code should prefer the ordinary persistent semantics.

## Explicit save

```cpp
PersistentSettings.save();
```

serializes the current RuntimeState as JSON and writes:

```text
/configs.json
```

Because normal persistent `set()` and `remove()` already save, explicit save is mainly useful after an intentional in-memory-only mutation path.

## Explicit load

```cpp
PersistentSettings.load();
```

replaces the current in-memory entries with the file contents.

Settings loading uses ArduinoJson 7's dynamically sized `JsonDocument`;
there is no fixed 4096-byte parsing buffer.

The RuntimeState entry limit of 64 still applies while loading, and allocation
failure or invalid JSON causes the load to fail.

## Framework-private settings

NightMare currently stores internal values in PersistentSettings.

Examples include:

```text
_device_name
_timezone
_pending_identity_cleanup
_ssid
_password
```

These names are implementation details.

NightMare source refers to them through `NightMare::PersistentKey` constants;
the String values on disk remain unchanged for existing devices.

Application code should use the owning module APIs:

```text
DeviceIdentity
WiFi helpers
```

rather than editing private keys directly.

## CONFIG command

The generic command surface can query/update PersistentSettings:

```text
CONFIG GET ...
CONFIG SET ...
CONFIG SAVE
```

That does not make every stored key a stable public configuration contract.

The command is a generic store interface.

Module-specific invariants still belong to the module API.

For example, changing `_device_name` directly would bypass adoption/cleanup semantics, while changing `_timezone` directly would not apply `TZ` or refresh identity publications. Use the identity API or commands instead.

## Current coupling

The current persistence implementation is directly tied to LittleFS and String key/value data.

A future persistence abstraction may separate generic settings semantics from ESP32 filesystem storage.

Until then, applications should depend on the public store/module APIs rather than the backing-file implementation.
