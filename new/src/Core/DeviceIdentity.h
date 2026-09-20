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

    // Persists a new name. Once a resource or transport uses the address, the
    // change takes effect on the next boot so existing topics and the MQTT will
    // keep the same address for the current session.
    bool changeDeviceName(const String &newName);
    void lockAddress() { addressLocked_ = true; }

private:
    static bool validName(const String &name);
    String deviceName_;
    String deviceId_;
    bool initialized_ = false;
    bool addressLocked_ = false;
};

extern DeviceIdentity gDeviceIdentity;
