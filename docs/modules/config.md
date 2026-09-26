---
title: Config
description: Typed local configuration values, command ingress, and the Config declaration manifest.
section: modules
order: 25
---

# Config

Config values are local parameters that change application behavior. They are
not Resources and have no MQTT, publication, subscription, persistence, or
freshness semantics.

## Declaration and registration

Declare Configs as long-lived objects and bind them during setup:

```cpp
#include <NightMare/Config.h>

Config<uint32_t> maxDoorOpen("max_door_open_time");
Config<bool> autoOffEnabled("auto_off_enabled");
Config<uint32_t> restartRequiredOption("some_option", true);

void setup()
{
    gConfigManager.bind(&maxDoorOpen);
    gConfigManager.bind(&autoOffEnabled);
    gConfigManager.bind(&restartRequiredOption);
}
```

The manager stores non-owning pointers. A Config must outlive its binding.
Names must be non-empty and contain no command whitespace; names and pointers
may only be bound once. Up to 64 Configs may be bound.

## Local access

```cpp
const uint32_t current = maxDoorOpen.value();
maxDoorOpen.set(300000);
```

Local `set()` assigns the typed value directly. It does not call the ingress
handler, persist, publish, or reboot.

## String ingress

The common command path accepts:

```text
CONFIG LIST
CONFIG GET max_door_open_time
CONFIG SET max_door_open_time 300000
CONFIG MANIFEST
```

It redirects everything after `CONFIG` to `gConfigManager.handle(command)`,
whose direct API accepts the same command without the prefix:

```text
list
get max_door_open_time
set max_door_open_time 300000
manifest
```

`list` returns compact JSON declaration metadata. `get` returns the value in
its `NetCodec<T>` text form. `set` treats everything after the name separator
as the payload. `manifest` returns Base64 of the binary manifest.

The application may install one global ingress handler:

```cpp
bool onConfigChange(const String &key, const String &rawValue)
{
    if (key == "max_door_open_time" && rawValue.toInt() < 1000)
        return false;
    return true;
}

gConfigManager.setChangeHandler(onConfigChange);
```

Ingress first decodes the payload to the Config's declared type, then calls the
handler with the original payload, then commits the decoded value. Decode or
handler failure leaves the current value unchanged.

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
