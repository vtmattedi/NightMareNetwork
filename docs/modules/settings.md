---
title: Settings and runtime state
description: In-memory RuntimeState and persistence-backed StateStore behavior.
section: modules
order: 60
---

# Settings and runtime state

NightMare currently has two closely related key/value stores:

```text
RuntimeState
    RAM only

StateStore
    RuntimeState behavior + optional persistence
```

The standard global instances are:

```cpp
SystemState
PersistentSettings
```

They should not be treated as interchangeable merely because their APIs are similar.

## RuntimeState

`RuntimeState` is a small in-memory String key/value store.

```cpp
RuntimeState state;
```

or use the global:

```cpp
SystemState
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

is framework/runtime state.

Current framework examples include:

```text
LittleFS_mounted
time_synced
boot_time
```

Applications should avoid assuming undocumented framework keys are stable API.

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
2. updates `SystemState["LittleFS_mounted"]`,
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

The current JSON parsing buffer is:

```text
4096 bytes
```

The RuntimeState entry limit of 64 still applies while loading.

## Framework-private settings

NightMare currently stores internal values in PersistentSettings.

Examples include:

```text
_device_name
_pending_identity_cleanup
_ssid
_password
```

These names are implementation details.

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

For example, changing `_device_name` directly would bypass adoption/cleanup semantics and should not be used as an identity-management method.

## SYSTEMCONFIGS command

`SYSTEMCONFIGS` operates on:

```cpp
SystemState
```

rather than PersistentSettings.

Those values disappear on reboot.

## Current coupling

The current persistence implementation is directly tied to LittleFS and String key/value data.

A future persistence abstraction may separate generic settings semantics from ESP32 filesystem storage.

Until then, applications should depend on the public store/module APIs rather than the backing-file implementation.
