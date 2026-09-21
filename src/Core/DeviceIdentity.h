#pragma once

#include <Arduino.h>

// Owns this device's MQTT address. Namespaces are not part of the current topic format.
class DeviceIdentity
{
public:
    bool begin();
    const String &getDeviceName();
    const String &getDeviceId();
    bool isDevice(const String &topic);
    bool relativeTopic(const String &topic, String &relative);
    String topic(const String &relative);
    void onNameChanged(void (*callback)(const String &newName));
    // With settings enabled, persists a new name. Once a resource or transport
    // uses the address, the change takes effect on the next boot. Without
    // settings, changes are limited to the current boot and must precede locking.
    bool changeDeviceName(const String &newName);
    void lockAddress();
    bool changeName(const String &newName);
private:
    static bool validName(const String &name);
    String deviceName_;
    String deviceId_;
    bool initialized_ = false;
    bool addressLocked_ = false;
    void (*nameChangedCallback_)(const String &newName) = nullptr;
};

extern DeviceIdentity gDeviceIdentity;
