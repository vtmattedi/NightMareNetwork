#pragma once
#include <NightMare/Storage/SettingsStore.h>

namespace NightMare {

class DeviceIdentity {
public:
    // hardwareSuffix is a stable, printable chip/MAC suffix supplied by the platform.
    bool begin(SettingsStore& settings, const String& hardwareSuffix);
    const String& id() const { return _id; }
    const String& label() const { return _label; }
    bool setLabel(const String& label);
    // Persists a new protocol ID; active network addressing changes on next boot.
    bool adoptId(const String& id);
    static bool validId(const String& id);
private:
    SettingsStore* _settings = nullptr;
    String _id;
    String _label;
};

} // namespace NightMare
