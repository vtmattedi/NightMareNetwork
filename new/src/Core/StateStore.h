#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <FS.h>
#include <ArduinoJson.h>

#define STATE_STORE_MAX_ENTRIES 64
// Keep the existing path so stored settings survive the source rename.
#define SETTINGS_FILE "/configs.json"

class StateStore
{
public:
    struct Entry
    {
        String key;
        String value;
    };

private:
    bool persistent_ = true;
    Entry entries[STATE_STORE_MAX_ENTRIES];
    int count = 0;
    bool initialized = false;

public:
    explicit StateStore(bool persistent = true);

    bool begin();
    bool load();
    bool save();

    bool set(const String &key, const String &value);
    String get(const String &key, const String &defaultValue = "");
    bool getFlag(const String &key);
    bool setFlag(const String &key, bool value);
    bool exists(const String &key);
    bool remove(const String &key);
    void clear(bool saveChanges = false);
    int size() const { return count; }
    String toJson();
};

/// Persistent settings stored in SETTINGS_FILE.
extern StateStore PersistentSettings;

/// Runtime system status, held only in memory.
extern StateStore SystemState;

/// Returns the current runtime device name.
const char *getDeviceName();
