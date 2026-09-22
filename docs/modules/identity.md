---
title: Device identity
description: Logical names, physical hardware signatures, adoption, and old-identity cleanup.
section: modules
order: 20
---

# Device identity

`DeviceIdentity` owns the address NightMare uses for this device on MQTT.

The public singleton is:

```cpp
gDeviceIdentity
```

Identity is intentionally split into three values because a logical network name and a physical board identity are not the same thing.

## Device name

```cpp
const String &getDeviceName();
```

The device name is the current logical/network identity.

Example:

```text
bedroom-ac
```

It is used as the root of device-scoped MQTT topics:

```text
bedroom-ac/status
bedroom-ac/resources
bedroom-ac/info
```

The name is adoptable.

## Hardware signature

```cpp
const String &getHardwareSignature();
```

The hardware signature is the generated name the board was born with.

Example:

```text
Esp32-nm-6ca172e0
```

It is based on the board's hardware ID and is the same value used as the default logical device name before adoption.

It does **not** change when the logical device name changes.

This is why status can identify both:

```json
{
  "name": "bedroom-ac",
  "hardware": "Esp32-nm-6ca172e0",
  "online": true
}
```

## Device ID

```cpp
const String &getDeviceId();
```

The device ID is a raw machine-oriented hardware identifier derived from `ESP.getEfuseMac()`.

It is formatted as 16 hexadecimal characters representing the 64-bit value.

Use it where a low-level stable identifier is required.

Do not treat it as the human-facing MQTT name.

## Initialization

```cpp
gDeviceIdentity.begin();
```

Initialization:

1. reads the ESP hardware ID,
2. derives the hardware signature/default name,
3. initializes persistent settings when available,
4. loads the persisted logical name,
5. validates it,
6. falls back to the generated default when invalid,
7. loads any pending identity-cleanup record.

The getters call `begin()` internally, so explicit initialization is normally handled by the framework lifecycle.

## Default name

The default name is generated as:

```text
Esp32-nm-<low-32-bits-of-MAC-in-hex>
```

The exact example depends on the board.

This default is also the hardware signature.

## Valid device names

Use:

```cpp
DeviceIdentity::validDeviceName(name);
```

A valid device name:

- contains 1 to 64 characters,
- is not exactly `all`,
- contains no `/`,
- contains no `+`,
- contains no `#`,
- contains no control character below `0x20`.

The reserved value:

```text
all
```

is used by the broadcast command topic:

```text
all/console/in
```

## Topic helpers

Build a device-relative topic with:

```cpp
gDeviceIdentity.topic("status");
```

For device name `bedroom-ac`:

```text
bedroom-ac/status
```

A leading slash is also accepted:

```cpp
gDeviceIdentity.topic("/status");
```

and produces the same logical shape.

## Testing whether a topic belongs to this device

```cpp
gDeviceIdentity.isDevice(fullTopic);
```

returns true for the exact device name or a child topic under it.

Example for `bedroom-ac`:

```text
bedroom-ac
bedroom-ac/status
bedroom-ac/resources/power/state
```

## Relative topic

```cpp
String relative;

if (gDeviceIdentity.relativeTopic(fullTopic, relative))
{
    // relative has the device prefix removed
}
```

Example:

```text
full:
bedroom-ac/custom/state

relative:
custom/state
```

The exact bare device root does not produce a relative child path.

## Address locking

```cpp
gDeviceIdentity.lockAddress();
```

marks the current logical address as already in active use for this boot.

NightMare calls this when network identity begins to matter, including Managed Resource binding and WiFi/MQTT initialization.

Address locking prevents adoption from trying to migrate a live topic namespace in place.

## Adoption

Request a new logical name with:

```cpp
gDeviceIdentity.beginAdoption("bedroom-ac");
```

Adoption is treated as a migration, not as an ordinary String assignment.

The operation records:

```text
new logical name
old logical name
which old retained artifacts still need cleanup
```

Only one migration can be pending at a time.

A second adoption is refused until cleanup of the previous identity has completed.

## Adoption before address lock

If the address has not yet been locked, a successful adoption can change the in-memory device name immediately:

```text
old -> new
```

The old identity is still recorded for cleanup.

## Adoption after address lock

If the address is already locked, NightMare does not attempt a live migration.

Instead:

```text
persist new name
remember old identity
continue current boot under old name
reboot
start next boot under new name
clean old retained network state
```

This avoids trying to atomically migrate:

- MQTT Last Will,
- subscriptions,
- queued traffic,
- Resource state,
- manifest,
- status,
- application assumptions.

## Persistence keys

The current implementation stores framework identity state in PersistentSettings under private keys including:

```text
_device_name
_pending_identity_cleanup
```

These are implementation details.

Applications should use the `DeviceIdentity` API rather than editing these keys directly.

## Pending cleanup

Public cleanup state is represented by:

```cpp
struct PendingIdentityCleanup
{
    String oldName;
    uint8_t pendingFlags;
};
```

Current cleanup participants are:

```cpp
CLEANUP_RESOURCES
CLEANUP_STATUS
```

The pending record is persistent when settings are enabled.

## Cleanup lifecycle

The network-side coordinator is:

```cpp
processPendingIdentityCleanup();
```

It asks `DeviceIdentity` which parts still need cleanup and performs them through the appropriate network owners.

`DeviceIdentity` stores state.

It does not publish MQTT itself.

## Resource cleanup

Resource cleanup removes retained state under the old identity for:

- every currently declared Managed Value,
- the old Resource manifest.

Actions need no retained cleanup because `/invoke` is transient.

A Managed Value removed from the firmware before cleanup cannot be discovered from the current registry and therefore cannot be automatically tombstoned.

## Status cleanup

Old status cleanup intentionally does two publications.

First:

```json
{
  "name": "<old-name>",
  "hardware": "<same-physical-board>",
  "online": false
}
```

is published retained.

Then an empty retained payload tombstones:

```text
<old-name>/status
```

This lets active observers see a normal offline transition before the retained ghost is removed.

## Retry behavior

`startNightMareESP()` checks for pending cleanup.

When Scheduler + MQTT are enabled, it installs a MANAGED recurring callback job:

```text
_nm_identity_cleanup
```

at:

```text
NM_IDENTITY_CLEANUP_RETRY_MS
```

default:

```text
60000 ms
```

The job stays installed while cleanup is pending.

It removes itself when cleanup completes or there is nothing left to do.

Because it is MANAGED, `JOB CLEAR` cannot remove it.

## Cleanup waits for the new identity to be active

Immediately after a locked adoption, the current boot still runs under the old name.

At that point the old retained state is still live and must not be deleted.

`getPendingIdentityCleanup()` therefore reports no actionable cleanup while:

```text
pendingOldName == current deviceName
```

Cleanup becomes actionable after reboot when the current name is the new identity.

## Transactional cleanup bits

Each cleanup participant clears its own bit only after its operation succeeds.

If persisting the updated cleanup record fails, the in-memory state is rolled back so the next attempt still knows that cleanup is pending.

The migration is considered complete only when all pending bits are cleared.
