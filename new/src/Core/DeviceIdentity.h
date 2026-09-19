/*
 * Device Name,
 * Device NameSpace,
 * ChangeDeviceName,
 * IsDevice(Topic)
 * IsNamespace(Topic)
 */

#pragma once
#include <Arduino.h>
#include <StateStore.h>

struct DeviceIdentity
{
private:
    String deviceName;
    String deviceNamespace;

public:
    DeviceIdentity();
    bool isDevice(const String &topic) const;
    bool isNamespace(const String &topic) const;
    void changeDeviceName(const String &newName);
    void changeDeviceNamespace(const String &newNamespace);
    const String &getDeviceName() const { return deviceName; }
    const String &getDeviceNamespace() const { return deviceNamespace; }
};

extern DeviceIdentity gDeviceIdentity;