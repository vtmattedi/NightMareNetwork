#include "DeviceIdentity.h"


DeviceIdentity gDeviceIdentity;

DeviceIdentity::DeviceIdentity() {
    //default device name is:
    // NM-Device-<EfuseMac>
    const char* defaultDeviceName = "NM-Device-";
    String _deviceName = PersistentSettings.get("_device_name", defaultDeviceName + String((uint32_t)ESP.getEfuseMac(), HEX));
    String _deviceNamespace = PersistentSettings.get("_device_namespace", "");
    deviceName = _deviceName;
    deviceNamespace = _deviceNamespace;
}

DeviceIdentity.changeDeviceName(const String &newName) {
    deviceName = newName;
    PersistentSettings.set("_device_name", newName);
}

DeviceIdentity.changeDeviceNamespace(const String &newNamespace) {
    deviceNamespace = newNamespace;
    PersistentSettings.set("_device_namespace", newNamespace);
}

DeviceIdentity::isDevice(const String &topic) const {
    // Check if the topic starts with the device name
    return topic.startsWith(deviceName);
}

DeviceIdentity::isNamespace(const String &topic) const {
    return false; // Placeholder implementation, adjust as needed
}

DeviceIdentity::getDeviceName() const {
    return deviceName;
}

DeviceIdentity::getDeviceNamespace() const {
    return deviceNamespace;
}