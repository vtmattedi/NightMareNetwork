#include "DeviceIdentity.h"
#include "PersistentKeys.h"
#include "NamespaceId.h"
#include <NightMare/Features.h>
#if NM_ENABLE_SETTINGS
#include "StateStore.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <esp_mac.h>
#include <cstring>

DeviceIdentity gDeviceIdentity;

namespace
{
// "<oldName>/<flags>". The separator is safe because '/' never appears in a
// device name, and one record is all v1 allows.
constexpr uint8_t AllCleanupFlags = CLEANUP_RESOURCES | CLEANUP_STATUS | CLEANUP_DOCUMENTS;
constexpr char AddressModel[] = "2";

bool applyTimezone(const std::string &timezone)
{
    if (setenv("TZ", timezone.c_str(), 1) != 0)
        return false;
    tzset();
    return true;
}
}

bool DeviceIdentity::validDeviceName(const std::string &name)
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

bool DeviceIdentity::validLabel(const std::string &label)
{
    if (label.length() > 64)
        return false;
    for (size_t i = 0; i < label.length(); ++i)
    {
        const uint8_t c = static_cast<uint8_t>(label[i]);
        if (c < 0x20 || c == 0x7F)
            return false;
    }
    return true;
}

bool DeviceIdentity::validTimezone(const std::string &timezone)
{
    if (timezone.length() == 0 || timezone.length() > 128)
        return false;
    for (size_t i = 0; i < timezone.length(); ++i)
    {
        const uint8_t c = static_cast<uint8_t>(timezone[i]);
        if (c < 0x20 || c == 0x7F)
            return false;
    }
    return true;
}

bool DeviceIdentity::begin()
{
    if (initialized_)
        return true;
    uint8_t macBytes[6] = {};
    if (esp_efuse_mac_get_default(macBytes) != ESP_OK)
        return false;
    uint64_t mac = 0;
    std::memcpy(&mac, macBytes, sizeof(macBytes));
    char id[17];
    // uint32_t is long on these toolchains, so %x would be the wrong conversion.
    snprintf(id, sizeof(id), "%08lx%08lx", static_cast<unsigned long>(mac >> 32),
             static_cast<unsigned long>(mac & 0xFFFFFFFFUL));
    deviceId_ = id;

    // Keep the existing default prefix and persisted key for existing devices.
    const std::string defaultName = std::string("Esp32-nm-") + NightMare::Text::hex(static_cast<uint32_t>(mac));
    hardwareSignature_ = defaultName;
    std::string storedName = defaultName;
    const std::string configuredTimezone = std::string(NM_TIMEZONE);
    const std::string defaultTimezone = validTimezone(configuredTimezone)
                                       ? configuredTimezone : std::string("UTC0");
    std::string storedTimezone = defaultTimezone;
#if NM_ENABLE_SETTINGS
    const bool settingsReady = PersistentSettings.begin();
    if (settingsReady)
    {
        storedName = PersistentSettings.get(NightMare::PersistentKey::DeviceName, defaultName);
        storedTimezone = PersistentSettings.get(NightMare::PersistentKey::DeviceTimezone,
                                                defaultTimezone);
        const std::string storedLabel =
            PersistentSettings.get(NightMare::PersistentKey::DeviceLabel, std::string());
        label_ = validLabel(storedLabel) ? storedLabel : std::string();
        migrateToAddressModel(defaultName, storedName);
    }
#endif
    deviceName_ = validDeviceName(storedName) ? storedName : defaultName;
    timezone_ = validTimezone(storedTimezone) ? storedTimezone : defaultTimezone;
    if (!applyTimezone(timezone_))
        return false;
    namespace_ = storedNamespace();
    topicRoot_ = storedTopicRoot();
    initialized_ = true;
#if NM_ENABLE_SETTINGS
    if (settingsReady &&
        (!PersistentSettings.exists(NightMare::PersistentKey::DeviceName) ||
         storedName != deviceName_))
        PersistentSettings.set(NightMare::PersistentKey::DeviceName, deviceName_);
    if (settingsReady &&
        (!PersistentSettings.exists(NightMare::PersistentKey::DeviceTimezone) ||
         storedTimezone != timezone_))
        PersistentSettings.set(NightMare::PersistentKey::DeviceTimezone, timezone_);
    if (settingsReady)
        loadPendingCleanup();
#endif
    return true;
}

// Before the address/label split the stored name was both the topic root and
// what people read. Once per board: that name becomes the label, the address
// goes back to the hardware signature, and the old root is cleaned up like an
// adoption's (status, Resource states and manifests, framework documents).
void DeviceIdentity::migrateToAddressModel(const std::string &defaultName, std::string &storedName)
{
#if NM_ENABLE_SETTINGS
    if (PersistentSettings.get(NightMare::PersistentKey::IdentityModel, std::string()) == AddressModel)
        return;
    if (validDeviceName(storedName) && storedName != defaultName)
    {
        if (label_.empty() && validLabel(storedName))
        {
            label_ = storedName;
            PersistentSettings.set(NightMare::PersistentKey::DeviceLabel, label_);
        }
        if (!PersistentSettings.exists(NightMare::PersistentKey::PendingIdentityCleanup))
            PersistentSettings.set(NightMare::PersistentKey::PendingIdentityCleanup,
                                   storedName + "/" + std::to_string(AllCleanupFlags));
        else
            LOG_WARNING("ID", "Identity cleanup already pending; '%s' is not cleaned up",
                        storedName.c_str());
        LOG("ID", "Address model: '%s' is now the label, the address is %s",
            storedName.c_str(), defaultName.c_str());
        storedName = defaultName;
        PersistentSettings.set(NightMare::PersistentKey::DeviceName, defaultName);
    }
    PersistentSettings.set(NightMare::PersistentKey::IdentityModel, AddressModel);
#else
    (void)defaultName;
    (void)storedName;
#endif
}

void DeviceIdentity::loadPendingCleanup()
{
#if NM_ENABLE_SETTINGS
    if (!PersistentSettings.exists(NightMare::PersistentKey::PendingIdentityCleanup))
        return;
    const std::string record = PersistentSettings.get(
        NightMare::PersistentKey::PendingIdentityCleanup, std::string());
    const int separator = NightMare::Text::lastIndexOf(record, '/');
    if (separator > 0)
    {
        const std::string oldName = NightMare::Text::slice(record, 0, separator);
        const long flags = NightMare::Text::toLong(NightMare::Text::slice(record, separator + 1));
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
    PersistentSettings.remove(NightMare::PersistentKey::PendingIdentityCleanup);
#endif
}

bool DeviceIdentity::persistPendingCleanup()
{
#if NM_ENABLE_SETTINGS
    if (pendingFlags_ == 0)
        return !PersistentSettings.exists(NightMare::PersistentKey::PendingIdentityCleanup) ||
               PersistentSettings.remove(NightMare::PersistentKey::PendingIdentityCleanup);
    return PersistentSettings.set(NightMare::PersistentKey::PendingIdentityCleanup,
                                  pendingOldName_ + "/" + std::to_string(pendingFlags_));
#else
    return true;
#endif
}

const std::string &DeviceIdentity::getDeviceName()
{
    begin();
    return deviceName_;
}

const std::string &DeviceIdentity::getLabel()
{
    begin();
    return label_;
}

const std::string &DeviceIdentity::getDisplayName()
{
    begin();
    return label_.empty() ? deviceName_ : label_;
}

std::string DeviceIdentity::hostname()
{
    std::string host;
    bool dash = false;
    for (char c : getDisplayName())
    {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        const bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (keep)
        {
            host += c;
            dash = false;
        }
        else if (!host.empty() && !dash)
        {
            host += '-';
            dash = true;
        }
        if (host.length() >= 32)
            break;
    }
    while (!host.empty() && host.back() == '-')
        host.pop_back();
    return host.empty() ? deviceName_ : host;
}

bool DeviceIdentity::setLabel(const std::string &label)
{
    if (!begin() || !validLabel(label))
        return false;
    if (label == label_)
        return true;
#if NM_ENABLE_SETTINGS
    const bool saved = label.empty()
                           ? (!PersistentSettings.exists(NightMare::PersistentKey::DeviceLabel) ||
                              PersistentSettings.remove(NightMare::PersistentKey::DeviceLabel))
                           : PersistentSettings.set(NightMare::PersistentKey::DeviceLabel, label);
    if (!saved)
        return false;
#endif
    label_ = label;
    return true;
}

const std::string &DeviceIdentity::getDeviceId()
{
    begin();
    return deviceId_;
}

const std::string &DeviceIdentity::getHardwareSignature()
{
    begin();
    return hardwareSignature_;
}

const std::string &DeviceIdentity::getTimezone()
{
    begin();
    return timezone_;
}

bool DeviceIdentity::setTimezone(const std::string &timezone)
{
    if (!begin() || !validTimezone(timezone))
        return false;
    if (timezone == timezone_)
        return true;
    const std::string previousTimezone = timezone_;
    if (!applyTimezone(timezone))
        return false;
#if NM_ENABLE_SETTINGS
    if (!PersistentSettings.set(NightMare::PersistentKey::DeviceTimezone, timezone))
    {
        // StateStore mutates its in-memory value before saving. Restore both
        // views even when the first filesystem write failed.
        PersistentSettings.set(NightMare::PersistentKey::DeviceTimezone, previousTimezone);
        applyTimezone(previousTimezone);
        return false;
    }
#endif
    timezone_ = timezone;
    return true;
}

bool DeviceIdentity::isDevice(const std::string &fullTopic)
{
    const std::string &name = getDeviceName();
    return fullTopic == name || NightMare::Text::startsWith(fullTopic, name + "/");
}

bool DeviceIdentity::relativeTopic(const std::string &fullTopic, std::string &relative)
{
    if (!isDevice(fullTopic) || fullTopic.length() == deviceName_.length())
        return false;
    relative = NightMare::Text::slice(fullTopic, deviceName_.length() + 1);
    return true;
}

std::string DeviceIdentity::topic(const std::string &relative)
{
    const std::string &name = getDeviceName();
    if (NightMare::Text::startsWith(relative, "/"))
        return name + relative;
    return name + "/" + relative;
}

void DeviceIdentity::lockAddress()
{
    addressLocked_ = true;
}

bool DeviceIdentity::beginAdoption(const std::string &newName)
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

    const std::string oldName = deviceName_;
#if NM_ENABLE_SETTINGS
    // The cleanup record goes first. If power fails before the name is written,
    // begin() finds a record naming the current identity and discards it; in
    // the other order a rename could survive without anything to clean it up.
    pendingOldName_ = oldName;
    pendingFlags_ = AllCleanupFlags;
    if (!persistPendingCleanup())
    {
        pendingOldName_ = std::string();
        pendingFlags_ = 0;
        return false;
    }
    if (!PersistentSettings.set(NightMare::PersistentKey::DeviceName, newName))
    {
        pendingOldName_ = std::string();
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

// A bit only counts as done once the record on flash says so. If that write
// fails, memory goes back to match it, so this boot retries too instead of
// believing a completion the next boot would not know about.
bool DeviceIdentity::markIdentityCleanupComplete(IdentityCleanupFlags flag)
{
    begin();
    if ((pendingFlags_ & flag) == 0)
        return true;

    const uint8_t previousFlags = pendingFlags_;
    const std::string previousName = pendingOldName_;
    pendingFlags_ = static_cast<uint8_t>(pendingFlags_ & ~flag);
    if (pendingFlags_ == 0)
        pendingOldName_ = std::string();
    if (persistPendingCleanup())
        return true;

    pendingFlags_ = previousFlags;
    pendingOldName_ = previousName;
    return false;
}

bool DeviceIdentity::validNamespace(const std::string &id)
{
    return NightMare::NamespaceId::validNamespace(id.c_str()) && id.find('\0') == std::string::npos;
}

bool DeviceIdentity::validTopicRoot(const std::string &root)
{
    return NightMare::NamespaceId::validTopicRoot(root.c_str()) && root.find('\0') == std::string::npos;
}

const std::string &DeviceIdentity::getNamespace()
{
    begin();
    return namespace_;
}

const std::string &DeviceIdentity::getTopicRoot()
{
    begin();
    return topicRoot_;
}

// A stored value that no longer validates falls back to the compile-time default, so a
// corrupt file can never produce an invalid namespace or root.
std::string DeviceIdentity::storedNamespace()
{
    const std::string compiled = NM_NAMESPACE;
#if NM_ENABLE_SETTINGS
    if (PersistentSettings.exists(NightMare::PersistentKey::Namespace))
    {
        const std::string stored = PersistentSettings.get(NightMare::PersistentKey::Namespace);
        if (stored.empty() || validNamespace(stored))
            return stored;
    }
#endif
    return compiled;
}

std::string DeviceIdentity::storedTopicRoot()
{
    const std::string compiled = NM_TOPIC_ROOT;
#if NM_ENABLE_SETTINGS
    if (PersistentSettings.exists(NightMare::PersistentKey::TopicRoot))
    {
        const std::string stored = PersistentSettings.get(NightMare::PersistentKey::TopicRoot);
        if (validTopicRoot(stored))
            return stored;
    }
#endif
    return compiled;
}

bool DeviceIdentity::setNamespace(const std::string &id, const std::string &root)
{
    if (!begin() || (!id.empty() && !validNamespace(id)) || (!root.empty() && !validTopicRoot(root)))
        return false;
#if NM_ENABLE_SETTINGS
    if (root.empty())
        return PersistentSettings.set(NightMare::PersistentKey::Namespace, id);
    const std::string keys[2] = {NightMare::PersistentKey::Namespace, NightMare::PersistentKey::TopicRoot};
    const std::string values[2] = {id, root};
    return PersistentSettings.setMany(keys, values, 2);
#else
    return false; // nothing to persist into
#endif
}

bool DeviceIdentity::resetNamespace()
{
    if (!begin())
        return false;
#if NM_ENABLE_SETTINGS
    const bool ns = !PersistentSettings.exists(NightMare::PersistentKey::Namespace) ||
                    PersistentSettings.remove(NightMare::PersistentKey::Namespace);
    const bool root = !PersistentSettings.exists(NightMare::PersistentKey::TopicRoot) ||
                      PersistentSettings.remove(NightMare::PersistentKey::TopicRoot);
    return ns && root;
#else
    return false;
#endif
}
