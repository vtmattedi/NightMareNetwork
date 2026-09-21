#pragma once

#include <Arduino.h>

/// @brief The parts of an old identity's network footprint still to be removed.
/// Each participant clears its own bit once its cleanup has actually succeeded.
enum IdentityCleanupFlags : uint8_t
{
    CLEANUP_RESOURCES = 1 << 0, // Retained manifest and value states (ResourcesManager).
    CLEANUP_STATUS = 1 << 1,    // Retained <old>/status (network lifecycle).
};

struct PendingIdentityCleanup
{
    String oldName;
    uint8_t pendingFlags = 0;
};

// Owns this device's MQTT address. Namespaces are not part of the current topic format.
//
// Adopting a new name is a migration, not a rename: the new name is persisted,
// the old one is remembered along with what still has to be cleaned up under
// it, and the migration only ends when that cleanup has succeeded. This class
// keeps that state; it never touches MQTT or the ResourcesManager itself.
class DeviceIdentity
{
public:
    bool begin();
    const String &getDeviceName();
    const String &getDeviceId();
    bool isDevice(const String &topic);
    bool relativeTopic(const String &topic, String &relative);
    String topic(const String &relative);
    void lockAddress();

    /// @brief A usable device name: a valid topic segment that is not the
    /// reserved broadcast name "all".
    static bool validDeviceName(const String &name);

    /// @brief Persists newName as this device's identity and records the current
    /// one for cleanup. Once the address is locked the running firmware keeps
    /// the current name until reboot; before that the switch is immediate.
    /// Refused while a previous adoption is still being cleaned up, so an old
    /// identity is never forgotten with retained data still under it.
    bool beginAdoption(const String &newName);

    /// @brief True from adoption until every participant has finished.
    bool hasPendingIdentityCleanup();
    /// @brief The cleanup to run now. False while there is none, and also while
    /// this boot still runs as the old name (a locked adoption before reboot):
    /// that data is still live and must not be withdrawn.
    bool getPendingIdentityCleanup(PendingIdentityCleanup &cleanup);
    /// @brief Clears one participant's bit, and the whole record with the last.
    bool markIdentityCleanupComplete(IdentityCleanupFlags flag);

private:
    bool persistPendingCleanup();
    void loadPendingCleanup();

    String deviceName_;
    String deviceId_;
    bool initialized_ = false;
    bool addressLocked_ = false;
    String pendingOldName_;
    uint8_t pendingFlags_ = 0;
};

extern DeviceIdentity gDeviceIdentity;
