#pragma once
#include <NightMare/Storage/SettingsStore.h>

namespace NightMare {

class Esp32SettingsPersistence final : public SettingsPersistence {
public:
    explicit Esp32SettingsPersistence(const char* path = "/configs.json") : _path(path) {}
    bool load(SettingsStore& store) override;
    bool save(const SettingsStore& store) override;
    bool mounted() const { return _mounted; }
    size_t totalBytes() const;
    size_t usedBytes() const;
private:
    const char* _path;
    bool _mounted = false;
};

} // namespace NightMare
