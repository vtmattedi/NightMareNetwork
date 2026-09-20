#include "DeviceIdentity.h"
#include "StateStore.h"
#include <stdio.h>

DeviceIdentity gDeviceIdentity;

bool DeviceIdentity::validName(const String &name)
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
    snprintf(id, sizeof(id), "%08x%08x", static_cast<uint32_t>(mac >> 32),
             static_cast<uint32_t>(mac));
    deviceId_ = id;

    // Keep the existing default prefix and persisted key for existing devices.
    const String defaultName = String("Esp32-nm-") + String(static_cast<uint32_t>(mac), HEX);
    const bool settingsReady = PersistentSettings.begin();
    const String storedName = settingsReady
                                  ? PersistentSettings.get("_device_name", defaultName)
                                  : defaultName;
    deviceName_ = validName(storedName) ? storedName : defaultName;
    initialized_ = true;
    if (settingsReady && (!PersistentSettings.exists("_device_name") || storedName != deviceName_))
        PersistentSettings.set("_device_name", deviceName_);
    return true;
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

bool DeviceIdentity::changeDeviceName(const String &newName)
{
    begin();
    if (!validName(newName) || !PersistentSettings.set("_device_name", newName))
        return false;
    if (!addressLocked_)
        deviceName_ = newName;
    return true;
}
