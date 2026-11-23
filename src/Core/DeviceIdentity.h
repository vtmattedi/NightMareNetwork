#pragma once

#include <Core/Text.h>
#include <Core/PlatformClock.h>
#include <Core/NamespaceId.h>

/// @brief The parts of an old identity's network footprint still to be removed.
/// Each participant clears its own bit once its cleanup has actually succeeded.
enum IdentityCleanupFlags : uint8_t
{
    CLEANUP_RESOURCES = 1 << 0, // Retained manifest and value states (ResourcesManager).
    CLEANUP_STATUS = 1 << 1,    // Retained <old>/status (network lifecycle).
    CLEANUP_DOCUMENTS = 1 << 2, // Retained <old>/info, hardware and telemetry documents.
};

struct PendingIdentityCleanup
{
    std::string oldName;
    uint8_t pendingFlags = 0;
};

// Owns this device's MQTT address, its display label and persisted identity
// configuration, including the namespace ID and topic root settings (NS-L1: stored and
// reported only; topics are not prefixed yet, see docs/architecture/namespaces.md).
//
// The address is the topic root. It is the hardware signature
// ("Esp32-nm-6ca172e0") unless deliberately moved, which is what replacing a
// board is for: the new board takes over the old one's address. The label is
// what people read ("Bedroom AC"): free text, changed at any time, published in
// status and info and never part of a topic, so renaming a device breaks no
// binding.
//
// Moving the address is a migration, not a rename: the new address is persisted,
// the old one is remembered along with what still has to be cleaned up under
// it, and the migration only ends when that cleanup has succeeded. This class
// keeps that state; it never publishes, calls the ResourcesManager or schedules
// anything. processPendingIdentityCleanup() does the work.
class DeviceIdentity
{
public:
    bool begin();
    /// @brief The address: the topic root, e.g. "Esp32-nm-6ca172e0". Moves only
    /// with an adoption (board replacement).
    const std::string &getDeviceName();
    const std::string &getAddress() { return getDeviceName(); }
    /// @brief The display label, e.g. "Bedroom AC". Empty when none was set.
    const std::string &getLabel();
    /// @brief The label, or the address when there is no label.
    const std::string &getDisplayName();
    /// @brief A DNS-safe host name derived from the display name (lowercase
    /// letters, digits and '-', at most 32), the address when nothing is left.
    std::string hostname();
    /// @brief Persists and applies a label at once; an empty one clears it.
    bool setLabel(const std::string &label);
    /// @brief At most 64 bytes, no control characters. Empty is allowed.
    static bool validLabel(const std::string &label);
    /// @brief The raw hardware ID, for machines.
    const std::string &getDeviceId();
    /// @brief The name this board was born with, e.g. "Esp32-nm-6ca172e0":
    /// generated from the hardware, identical to the default device name, and
    /// never changed by adoption. It says which physical board is behind a name.
    const std::string &getHardwareSignature();
    /// @brief The POSIX timezone used for local-time presentation.
    const std::string &getTimezone();
    bool isDevice(const std::string &topic);
    bool relativeTopic(const std::string &topic, std::string &relative);
    std::string topic(const std::string &relative);
    void lockAddress();

    /// @brief A usable device name: a valid topic segment that is not the
    /// reserved broadcast name "all".
    static bool validDeviceName(const std::string &name);

    /// @brief A non-empty printable POSIX timezone string, up to 128 characters.
    static bool validTimezone(const std::string &timezone);

    /// @brief Persists and immediately applies a POSIX timezone using TZ/tzset.
    /// Epoch timestamps remain UTC-based; only local-time presentation changes.
    bool setTimezone(const std::string &timezone);

    /// @brief The active namespace ID for this boot; empty is the legacy unprefixed layout.
    /// Persisted override first, else NM_NAMESPACE. Nothing prefixes topics with it yet (NS-L1).
    const std::string &getNamespace();
    /// @brief The active topic root for this boot ("nm" by default).
    const std::string &getTopicRoot();
    /// @brief What the next boot will use: the persisted override, else the compile-time default.
    std::string storedNamespace();
    std::string storedTopicRoot();
    /// @brief 1-16 characters of [a-z0-9-], starting with a letter.
    static bool validNamespace(const std::string &id);
    /// @brief One topic segment, 1-16 characters, no '/', '+', '#' or control characters.
    static bool validTopicRoot(const std::string &root);
    /// @brief Persists the namespace for the next boot (it never changes the running one). An
    /// empty id stores an explicit legacy choice; an empty root leaves the stored root alone.
    bool setNamespace(const std::string &id, const std::string &root = std::string());
    /// @brief Removes both stored values so the compile-time defaults apply at next boot.
    bool resetNamespace();

    /// @brief Persists newName as this device's identity and records the current
    /// one for cleanup. Once the address is locked the running firmware keeps
    /// the current name until reboot; before that the switch is immediate.
    /// Refused while a previous adoption is still being cleaned up, so an old
    /// identity is never forgotten with retained data still under it.
    bool beginAdoption(const std::string &newName);

    /// @brief True from adoption until every participant has finished.
    bool hasPendingIdentityCleanup();
    /// @brief The cleanup to run now. False while there is none, and also while
    /// this boot still runs as the old name (a locked adoption before reboot):
    /// that data is still live and must not be withdrawn.
    bool getPendingIdentityCleanup(PendingIdentityCleanup &cleanup);
    /// @brief Clears one participant's bit, and the whole record with the last.
    /// Transactional: false means nothing changed, in memory or on flash.
    bool markIdentityCleanupComplete(IdentityCleanupFlags flag);

private:
    bool persistPendingCleanup();
    void loadPendingCleanup();

    void migrateToAddressModel(const std::string &defaultName, std::string &storedName);
    std::string deviceName_;
    std::string label_;
    std::string deviceId_;
    std::string hardwareSignature_;
    std::string timezone_;
    std::string namespace_;
    std::string topicRoot_;
    bool initialized_ = false;
    bool addressLocked_ = false;
    std::string pendingOldName_;
    uint8_t pendingFlags_ = 0;
};

extern DeviceIdentity gDeviceIdentity;

namespace NightMare
{
/// @brief The effective namespace ID; empty means the legacy unprefixed layout.
inline const std::string &Namespace() { return gDeviceIdentity.getNamespace(); }
/// @brief The effective topic root, "nm" unless configured.
inline const std::string &TopicRoot() { return gDeviceIdentity.getTopicRoot(); }
}
