#pragma once

#include <NightMare/Features.h>
#include "RuntimeState.h"

#if NM_ENABLE_SETTINGS

// Keep the existing path so stored settings survive the source rename.
#define SETTINGS_FILE "/configs.json"

// Shares RuntimeState's storage and JSON behavior. Persistent instances add
// filesystem loading and save-on-change; nonpersistent instances stay in RAM.
class StateStore : public RuntimeState
{
public:
    explicit StateStore(bool persistent = true) : persistent_(persistent) {}

    bool begin();
    bool load();
    bool save();

    bool set(const String &key, const String &value) override;
    String get(const String &key, const String &defaultValue = "") const override;
    /// Sets several keys and writes the file once, so the group lands together.
    bool setMany(const String *keys, const String *values, size_t count);
    // Returns the value for the key if it exists, or (saves the default value) then return the default value if it does not exist.
    String getOrSave(const String &key, const String &defaultValue) const;
    bool exists(const String &key) const override;
    bool remove(const String &key) override;
    void clear() override;
    void clear(bool saveChanges);
    size_t size() const override;
    String toJson() const override;

private:
    bool persistent_;
    bool initialized_ = false;
};

extern StateStore PersistentSettings;

#endif // NM_ENABLE_SETTINGS
