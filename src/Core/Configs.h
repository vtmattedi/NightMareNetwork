#pragma once
#include <Modules.config.h>
#ifdef COMPILE_CONFIGS
#ifndef NIGHTMARE_CORE_CONFIGS_H
#define NIGHTMARE_CORE_CONFIGS_H

#include <Arduino.h>
#include <LittleFS.h>
#include <FS.h>
#include <ArduinoJson.h>

// Maximum number of configuration entries
#define CONFIGS_MAX_ENTRIES 64
// Maximum number of registered change callbacks
#define CONFIGS_MAX_CALLBACKS 8
// Configuration file path
#define CONFIGS_FILE "/configs.json"

class Configs
{
public:
    struct Entry
    {
        String key;
        String value;
    };

    // ---- Callback type ----
    typedef void (*ConfigCallback)(const String &key, const String &value);

private:
    bool saveAfterSet = true; // Automatically save to disk after each set() call
    Entry entries[CONFIGS_MAX_ENTRIES];
    int count = 0;
    bool initialized = false;
    ConfigCallback callbacks[CONFIGS_MAX_CALLBACKS] = {nullptr};
    int callbackCount = 0;
    void notifyCallbacks(const String &key, const String &value);

public:
    bool begin();
    bool load();
    bool save();

    bool set(const String &key, const String &value, bool privileged = false);
    String get(const String &key, const String &defaultValue = "", bool privileged = false);
    bool getFlag(const String &key, bool privileged = false);
    bool setFlag(const String &key, bool value, bool privileged = false);
    bool exists(const String &key, bool privileged = false);
    bool remove(const String &key, bool privileged = false);
    void clear(bool privileged = false, bool persistent = false);
    int size() const { return count; }
    void performanceTest();
    // ---- Callback registration ----
    int NotifyOnChange(ConfigCallback cb);
    bool UnnotifyChange(int callbackId);
    // Backward-compatible alias.
    bool onConfigSet(ConfigCallback cb) { return NotifyOnChange(cb); }

    // ---- Returns all settings as JSON ----
    String getAllSettings(bool previleged = false);

    // Constructor
    Configs(bool saveAfterSet = true);
};

/// @brief Global Configs instance (persistent storage)
extern Configs Config;

/// @brief Global System flags/states instance (non-persistent storage)
extern Configs SystemSettings;

/// @brief Returns current runtime device name.
const char *getDeviceName();


#endif
#endif