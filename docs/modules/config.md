---
title: Config
description: Typed local configuration values, command ingress, and the Config declaration manifest.
section: modules
order: 25
---

# Config

`Config<T>` represents durable application configuration. Declaring a Config
means the application intentionally exposes that value to the Config command
surface and expects accepted changes to survive reboot.

Configs are not Resources. They have no MQTT, publication, subscription, or
freshness semantics; persistence is local through `PersistentSettings`.

## Declaration and registration

Declare Configs with the lifetime their availability should have. Construction
automatically binds each object to the global manager, and destruction unbinds
it:

```cpp
#include <NightMare/Config.h>

Config<uint32_t> maxDoorOpen("door:max_open_time", 300000);
Config<bool> autoOffEnabled("door:auto_off", true);
Config<uint32_t> restartRequiredOption("system:some_option", 10, true);
Config<TimeType> quietStart("schedule:quiet_start", TimeType(22, 30));
Config<ColourType> statusColour("status:colour", ColourType(255, 0, 0));
```

The manager stores non-owning pointers. Automatic unbinding makes scoped Configs
safe. Names must be non-empty and contain no command whitespace; names and
pointers may only be bound once. Up to 64 Configs may be bound. Failed automatic
binding leaves the value readable but absent from persistence, command ingress,
and the manifest; its `set()` therefore returns `false`.

`configManager().bind()` and `configManager().unbind()` remain public for
explicit runtime registration. A Config that is already auto-bound is rejected
by `bind()` under the same duplicate pointer/name rules as any other binding.

Every Config declares its firmware default in the constructor. That value is
installed immediately. During startup, a valid persisted value overwrites the
default before normal framework use. `require_reboot` is the optional third
argument; this ordering avoids ambiguity for `Config<bool>`.

The built-in typed surface includes `TimeType` and `ColourType` through the
same `NetCodec<T>` path as primitive types. No Config-specific encoding exists.

## Local access

```cpp
const uint32_t current = maxDoorOpen.value(); // 300000 initially
maxDoorOpen.set(300000);
```

Local `set()` first persists the canonical `NetCodec<T>` encoding and then
updates the typed runtime value. It does not call either ingress handler,
publish, or reboot. If persistence fails, `set()` returns `false` and leaves
the runtime value unchanged.

Each Config may also install a typed handler for ConfigManager writes:

```cpp
bool acceptPeriod(Config<int> &config, const int &requested)
{
    return requested >= 15 && requested <= 86400;
}

Config<int> heartbeatPeriod("heartbeat:period", 15);

void setup()
{
    heartbeatPeriod.onWrite = acceptPeriod;
}
```

Returning `false` rejects the write and leaves the current value unchanged.
Like the global handler, `onWrite` is an ingress policy: a local `set()` does
not invoke it.

## Restore and storage lifecycle

Global/static Config construction never mounts the filesystem. The lifecycle
is deliberately split:

```text
Config construction
    -> install firmware default
    -> bind to ConfigManager

NightMare startup
    -> initialize PersistentSettings
    -> ConfigManager.restore()
    -> replace defaults with valid saved values
```

Before restore, Config values contain firmware defaults. After
`startNightMareESP()` (or an explicit successful `configManager().restore()`),
they contain persisted values when available. Applications should not treat
them as fully initialized before that phase completes.

Storage keys use the reserved `_config:` prefix. Values use the same canonical
`NetCodec<T>` encoding as command ingress. Missing values keep and persist the
firmware default. Invalid saved values log a warning, restore the default, and
replace the invalid storage entry. Restore does not invoke write handlers and
is idempotent after success.

Configs bound after a successful restore are not restored automatically. Bind
normal application Configs before framework startup.

## String ingress

The common command path accepts:

```text
CONFIG LIST
CONFIG GET max_door_open_time
CONFIG SET max_door_open_time 300000
CONFIG MANIFEST
```

It redirects everything after `CONFIG` to `configManager().handle(command)`,
whose direct API accepts the same command without the prefix:

```text
list
get max_door_open_time
set max_door_open_time 300000
manifest
```

`list` returns compact JSON containing declaration metadata and each Config's
current value in its canonical `NetCodec<T>` String form. `get` returns that
same encoded value for one Config. `set` treats everything after the name
separator as the payload. `manifest` returns Base64 of the declaration-only
binary manifest.

Example list item:

```json
{
  "name": "heartbeat:period",
  "type": "integer",
  "require_reboot": false,
  "value": "15"
}
```

The application may install one global ingress handler:

```cpp
bool onConfigChange(const String &key, const String &rawValue)
{
    if (key == "max_door_open_time" && rawValue.toInt() < 1000)
        return false;
    return true;
}

configManager().setChangeHandler(onConfigChange);
```

Ingress first decodes the payload to the Config's declared type, calls the
global handler with the original payload, then calls that Config's typed
`onWrite` handler when present. It then persists the canonical value and only
afterward commits the runtime value. Decode, handler, or persistence failure
leaves the current runtime value unchanged and reports an error.

## Naming related declarations

When related values naturally belong to one subsystem, prefer names such as
`<group>:<value>` or `<group>:<subgroup>:<value>`:

```text
led_strip:colour
led_strip:brightness
climate:bedroom:target_temperature
climate:bedroom:fan_speed
```

This is only a convention. NightMare does not enforce it or interpret `:` as a
protocol delimiter. It gives backend tools a stable hint for future visual
grouping and relationship inference; existing flat names remain valid.

## Manifest

The version 1 MessagePack layout is positional:

```text
[
  encodingVersion,
  manifestVersion,
  [
    [name, NetValueType, requireReboot],
    ...
  ]
]
```

Current values are intentionally absent: the manifest describes declarations,
not runtime state. `buildManifestMsgPack()` writes the canonical bytes into a
caller buffer. If capacity is insufficient it returns `false`, leaves the
buffer untouched, and reports the required length in `written`.

`buildManifestBase64()` and `handle("manifest")` directly encode those same
bytes. The manager never publishes the result.

`require_reboot` is informational only. Accepted values apply immediately;
the application or surrounding tooling decides whether and when to reboot.
