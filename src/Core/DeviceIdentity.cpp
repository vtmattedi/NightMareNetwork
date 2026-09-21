#include "DeviceIdentity.h"
#include <NightMare/Features.h>
#if NM_ENABLE_SETTINGS
#include "StateStore.h"
#endif
#include <stdio.h>

DeviceIdentity gDeviceIdentity;

namespace
{
constexpr char DeviceNameKey[] = "_device_name";
// "<oldName>/<flags>". The separator is safe because '/' never appears in a
// device name, and one record is all v1 allows.
constexpr char PendingCleanupKey[] = "_pending_identity_cleanup";
constexpr uint8_t AllCleanupFlags = CLEANUP_RESOURCES | CLEANUP_STATUS;
}

bool DeviceIdentity::validDeviceName(const String &name)
{
    if (name.length() == 0 || name.length() > 64 || name == "all")
        return false;
    for (size_t i = 0; i < name.length(); ++i)
    {
        const char c = name[i];
        if (c == '/' || c == '+' || c == '#' || static_cast<uint8_t>(c) < 0x20)
            return false;
    }
    return true;
}

bool DeviceIdentity::begin()
{
    if (initialized_)
        return true;
    const uint64_t mac = ESP.getEfuseMac();
    char id[17];
    // uint32_t is long on these toolchains, so %x would be the wrong conversion.
    snprintf(id, sizeof(id), "%08lx%08lx", static_cast<unsigned long>(mac >> 32),
             static_cast<unsigned long>(mac & 0xFFFFFFFFUL));
    deviceId_ = id;

    // Keep the existing default prefix and persisted key for existing devices.
    const String defaultName = String("Esp32-nm-") + String(static_cast<uint32_t>(mac), HEX);
    String storedName = defaultName;
#if NM_ENABLE_SETTINGS
    const bool settingsReady = PersistentSettings.begin();
    if (settingsReady)
        storedName = PersistentSettings.get(DeviceNameKey, defaultName);
#endif
    deviceName_ = validDeviceName(storedName) ? storedName : defaultName;
    initialized_ = true;
#if NM_ENABLE_SETTINGS
    if (settingsReady && (!PersistentSettings.exists(DeviceNameKey) || storedName != deviceName_))
        PersistentSettings.set(DeviceNameKey, deviceName_);
    if (settingsReady)
        loadPendingCleanup();
#endif
    return true;
}

void DeviceIdentity::loadPendingCleanup()
{
#if NM_ENABLE_SETTINGS
    if (!PersistentSettings.exists(PendingCleanupKey))
        return;
    const String record = PersistentSettings.get(PendingCleanupKey, String());
    const int separator = record.lastIndexOf('/');
    if (separator > 0)
    {
        const String oldName = record.substring(0, separator);
        const long flags = record.substring(separator + 1).toInt();
        // A record naming the current identity comes from a crash between the
        // two writes of an adoption: nothing moved, so there is nothing to clean,
        // and cleaning would delete this device's own live data.
        if (validDeviceName(oldName) && oldName != deviceName_ &&
            flags > 0 && flags <= AllCleanupFlags)
        {
            pendingOldName_ = oldName;
            pendingFlags_ = static_cast<uint8_t>(flags);
            return;
        }
    }
    PersistentSettings.remove(PendingCleanupKey);
#endif
}

bool DeviceIdentity::persistPendingCleanup()
{
#if NM_ENABLE_SETTINGS
    if (pendingFlags_ == 0)
        return !PersistentSettings.exists(PendingCleanupKey) ||
               PersistentSettings.remove(PendingCleanupKey);
    return PersistentSettings.set(PendingCleanupKey,
                                  pendingOldName_ + "/" + String(pendingFlags_));
#else
    return true;
#endif
}

const String &DeviceIdentity::getDeviceName()
{
    begin();
    return deviceName_;
}

const String &DeviceIdentity::getDeviceId()
{
    begin();
    return deviceId_;
}

bool DeviceIdentity::isDevice(const String &fullTopic)
{
    const String &name = getDeviceName();
    return fullTopic == name || fullTopic.startsWith(name + "/");
}

bool DeviceIdentity::relativeTopic(const String &fullTopic, String &relative)
{
    if (!isDevice(fullTopic) || fullTopic.length() == deviceName_.length())
        return false;
    relative = fullTopic.substring(deviceName_.length() + 1);
    return true;
}

String DeviceIdentity::topic(const String &relative)
{
    const String &name = getDeviceName();
    if (relative.startsWith("/"))
        return name + relative;
    return name + "/" + relative;
}

void DeviceIdentity::lockAddress()
{
    addressLocked_ = true;
}

bool DeviceIdentity::beginAdoption(const String &newName)
{
    begin();
    if (!validDeviceName(newName))
        return false;
    // Checked before the no-op case: while locked, the running name is still
    // the old one even though a different name is already persisted.
    if (pendingFlags_ != 0)
    {
        LOG_WARNING("ID", "Cannot adopt '%s': cleanup of '%s' is still pending",
                    newName.c_str(), pendingOldName_.c_str());
        return false;
    }
    if (newName == deviceName_)
        return true;

    const String oldName = deviceName_;
#if NM_ENABLE_SETTINGS
    // The cleanup record goes first. If power fails before the name is written,
    // begin() finds a record naming the current identity and discards it; in
    // the other order a rename could survive without anything to clean it up.
    pendingOldName_ = oldName;
    pendingFlags_ = AllCleanupFlags;
    if (!persistPendingCleanup())
    {
        pendingOldName_ = String();
        pendingFlags_ = 0;
        return false;
    }
    if (!PersistentSettings.set(DeviceNameKey, newName))
    {
        pendingOldName_ = String();
        pendingFlags_ = 0;
        persistPendingCleanup();
        return false;
    }
#else
    // Nothing survives a reboot, so a locked address could never move.
    if (addressLocked_)
        return false;
    pendingOldName_ = oldName;
    pendingFlags_ = AllCleanupFlags;
#endif
    // Addressing already in use is not migrated live: the new name waits for
    // the next boot, and cleanup of the old one runs on that boot's connection.
    if (!addressLocked_)
        deviceName_ = newName;
    return true;
}

bool DeviceIdentity::hasPendingIdentityCleanup()
{
    begin();
    return pendingFlags_ != 0;
}

bool DeviceIdentity::getPendingIdentityCleanup(PendingIdentityCleanup &cleanup)
{
    begin();
    if (pendingFlags_ == 0)
        return false;
    // After a locked adoption this boot still runs as the old name, so its
    // retained data is live, not leftover. Cleanup waits for the reboot.
    if (pendingOldName_ == deviceName_)
        return false;
    cleanup.oldName = pendingOldName_;
    cleanup.pendingFlags = pendingFlags_;
    return true;
}

bool DeviceIdentity::markIdentityCleanupComplete(IdentityCleanupFlags flag)
{
    begin();
    if ((pendingFlags_ & flag) == 0)
        return true;
    pendingFlags_ = static_cast<uint8_t>(pendingFlags_ & ~flag);
    if (pendingFlags_ == 0)
        pendingOldName_ = String();
    return persistPendingCleanup();
}
