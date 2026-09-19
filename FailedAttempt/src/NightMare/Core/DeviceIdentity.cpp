#include <NightMare/Core/DeviceIdentity.h>

namespace NightMare {

bool DeviceIdentity::validId(const String& id) {
    if (!id.length() || id.length() > 64) return false;
    for (size_t i = 0; i < id.length(); ++i) {
        char ch = id[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) return false;
    }
    return true;
}

bool DeviceIdentity::begin(SettingsStore& settings, const String& hardwareSuffix) {
    if (_settings || !hardwareSuffix.length()) return false;
    String stored = settings.getString("_device_id", "", SettingsAccess::Internal);
    if (!validId(stored)) {
        // Existing installations used this key for their stable protocol name.
        stored = settings.getString("_device_name", "", SettingsAccess::Internal);
    }
    if (!validId(stored)) stored = "esp32-nm-" + hardwareSuffix;
    if (!validId(stored) || !settings.setString("_device_id", stored, SettingsAccess::Internal))
        return false;
    _settings = &settings;
    _id = stored;
    _label = settings.getString("_device_label", "", SettingsAccess::Internal);
    return true;
}

bool DeviceIdentity::setLabel(const String& label) {
    if (!_settings || label.length() > 96 ||
        !_settings->setString("_device_label", label, SettingsAccess::Internal)) return false;
    _label = label;
    return true;
}

bool DeviceIdentity::adoptId(const String& id) {
    return _settings && validId(id) &&
           _settings->setString("_device_id", id, SettingsAccess::Internal);
}

} // namespace NightMare
