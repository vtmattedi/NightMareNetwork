---
title: Configs
description: The persistent key/value store and the volatile system-flags store, the privileged keys, change callbacks, and the flags the library itself keeps there.
section: modules
order: 30
---

# Configs — `Core/Configs.h`

Two instances of one class:

| instance | backing | for |
| --- | --- | --- |
| `Config` | `/configs.json` on LittleFS | anything that must survive a reboot: device name, controller settings, bindings |
| `SystemSettings` | RAM | runtime flags: `time_synced`, `ota_running`, `boot_time` |

Compiled with `COMPILE_CONFIGS`, which most other modules depend on.

## API

```cpp
bool   Config.begin();                 // mounts LittleFS, loads the file -- call first in setup()
bool   Config.load();
bool   Config.save();

bool   Config.set(key, value, privileged = false);
String Config.get(key, defaultValue = "", privileged = false);
bool   Config.setFlag(key, bool, privileged = false);
bool   Config.getFlag(key, privileged = false);
bool   Config.exists(key, privileged = false);
bool   Config.remove(key, privileged = false);
void   Config.clear(privileged = false, persistent = false);
String Config.getAllSettings(privileged = false);

int    Config.NotifyOnChange(ConfigCallback cb);   // (const String &key, const String &value)
bool   Config.UnnotifyChange(int id);
```

Values are `String`s; `setFlag`/`getFlag` store and read `"1"`/`"0"`. There
are `CONFIGS_MAX_ENTRIES` (64) slots and `CONFIGS_MAX_CALLBACKS` (8)
callbacks. `saveAfterSet` (constructor argument, default true) writes the file
on every `set`; controllers that set several keys at once call `save()`
themselves.

## Privileged keys

A key starting with `_` is privileged and is invisible to `get`/`set`/`exists`
unless the `privileged` flag is passed. On the console that is `-p`:

```
CONFIG GET ALL          # everything except _keys
CONFIG GET ALL -p       # everything
CONFIG SET _device_name "Adler" -p
```

The backend reads `config get ALL -p` on discovery and passes `-p` when the key
it is setting starts with `_`.

## The device name

`_device_name` is where the device's MQTT identity lives. On first boot
`initPersistentSettings()` creates it as `Esp32-nm-<efuse mac>`; a change
callback copies it into `DEVICE_NAME` so the MQTT client picks it up. Read it
with `getDeviceName()`; never with a compile-time constant. Renaming takes
effect on the next MQTT connect.

## Change callbacks

```cpp
Config.NotifyOnChange([](const String &key, const String &value) {
    if (key == "ac_hysteresis") gAcController.setHysteresis(value.toDouble());
});
```

Fired on every `set`, from whichever task called it — a `CONFIG SET` over MQTT
fires it on the MQTT task.

## The convention for a controller's settings

Namespace keys by controller, read each with its default in `init()`, and
**write the default back**, so a fresh unit's config file is complete and every
setting is visible and editable over `CONFIG GET ALL`:

```cpp
hysteresis = Config.get("ac_hysteresis", "0.5").toDouble();
Config.set("ac_hysteresis", String(hysteresis));
```

Anything that addresses another device is config, not code: `door_device`,
`door_key`, `door_invert`.

## Flags the library keeps in `SystemSettings`

| key | set by | meaning |
| --- | --- | --- |
| `time_synced` | time sync | the clock came from the backend at least once |
| `boot_time`, `boot_reason` | startup | reported by `BOOTINFO` and telemetry |
| `ota_running` | OTA | an upload is in progress — tasks that bit-bang a bus should sit it out |
| `ota_enabled` | OTA | reported in telemetry |
| `ioXpander_connected`, … | devices | whatever a device wants to report |

`SYSTEMCONFIGS GET ALL` on the console dumps them.
