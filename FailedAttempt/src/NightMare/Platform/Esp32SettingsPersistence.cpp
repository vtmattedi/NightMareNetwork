#include <NightMare/Features.h>
#if NIGHTMARE_ENABLE_SETTINGS
#include <NightMare/Platform/Esp32SettingsPersistence.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

namespace NightMare {

bool Esp32SettingsPersistence::load(SettingsStore& store) {
    if (!_mounted) _mounted = LittleFS.begin(true);
    if (!_mounted) return false;
    File file = LittleFS.open(_path, "r");
    if (!file) return true;
    DynamicJsonDocument document(8192);
    auto error = deserializeJson(document, file);
    file.close();
    if (error || !document.is<JsonObject>()) return false;
    for (JsonPairConst item : document.as<JsonObjectConst>()) {
        String value = item.value().as<String>();
        if (!store.setString(item.key().c_str(), value, SettingsAccess::Internal)) return false;
    }
    return true;
}

bool Esp32SettingsPersistence::save(const SettingsStore& store) {
    if (!_mounted) return false;
    DynamicJsonDocument document(8192);
    store.visit([](void* context, const String& key, const String& value) {
        (*static_cast<DynamicJsonDocument*>(context))[key] = value;
    }, &document, SettingsAccess::Internal);
    if (document.overflowed()) return false;
    File file = LittleFS.open(_path, "w");
    if (!file) return false;
    size_t written = serializeJson(document, file);
    file.close();
    return written > 0;
}

size_t Esp32SettingsPersistence::totalBytes() const { return _mounted ? LittleFS.totalBytes() : 0; }
size_t Esp32SettingsPersistence::usedBytes() const { return _mounted ? LittleFS.usedBytes() : 0; }

} // namespace NightMare
#endif
