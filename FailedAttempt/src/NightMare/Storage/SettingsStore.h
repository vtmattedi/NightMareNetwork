#pragma once
#include <Arduino.h>
#include <stdint.h>

#ifndef NIGHTMARE_MAX_SETTINGS
#define NIGHTMARE_MAX_SETTINGS 48
#endif

namespace NightMare {

class SettingsStore;

class SettingsPersistence {
public:
    virtual ~SettingsPersistence() = default;
    virtual bool load(SettingsStore& store) = 0;
    virtual bool save(const SettingsStore& store) = 0;
};

enum class StorageMode : uint8_t { Memory, Persistent };
enum class SettingsAccess : uint8_t { User, Internal };

class SettingsStore {
public:
    using Visitor = void (*)(void* context, const String& key, const String& value);
    SettingsStore(const char* name, StorageMode mode,
                  SettingsPersistence* persistence = nullptr);
    bool begin();
    const char* name() const { return _name; }
    StorageMode mode() const { return _mode; }
    bool setString(const char* key, const String& value,
                   SettingsAccess access = SettingsAccess::User);
    bool setBool(const char* key, bool value, SettingsAccess access = SettingsAccess::User);
    bool setInt(const char* key, int32_t value, SettingsAccess access = SettingsAccess::User);
    bool setUInt(const char* key, uint32_t value, SettingsAccess access = SettingsAccess::User);
    bool setFloat(const char* key, float value, SettingsAccess access = SettingsAccess::User);
    String getString(const char* key, const String& fallback = "",
                     SettingsAccess access = SettingsAccess::User) const;
    bool getBool(const char* key, bool fallback = false,
                 SettingsAccess access = SettingsAccess::User) const;
    int32_t getInt(const char* key, int32_t fallback = 0,
                   SettingsAccess access = SettingsAccess::User) const;
    uint32_t getUInt(const char* key, uint32_t fallback = 0,
                     SettingsAccess access = SettingsAccess::User) const;
    float getFloat(const char* key, float fallback = 0,
                   SettingsAccess access = SettingsAccess::User) const;
    bool exists(const char* key, SettingsAccess access = SettingsAccess::User) const;
    bool remove(const char* key, SettingsAccess access = SettingsAccess::User);
    bool clear(SettingsAccess access = SettingsAccess::User);
    void visit(Visitor visitor, void* context = nullptr,
               SettingsAccess access = SettingsAccess::User) const;
    size_t count(SettingsAccess access = SettingsAccess::User) const;
private:
    struct Entry { String key; String value; };
    bool allowed(const char* key, SettingsAccess access) const;
    int find(const char* key) const;
    bool flush();
    const char* _name;
    StorageMode _mode;
    SettingsPersistence* _persistence;
    Entry _entries[NIGHTMARE_MAX_SETTINGS];
    size_t _count = 0;
    bool _begun = false;
    bool _loading = false;
};

} // namespace NightMare
