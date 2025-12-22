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
    Entry entries[CONFIGS_MAX_ENTRIES];
    int count = 0;
    bool initialized = false;
    ConfigCallback callback = nullptr;

public:
    bool begin();
    bool load();
    bool save();

    bool set(const String &key, const String &value, bool privileged = false);
    String get(const String &key, const String &defaultValue = "");
    bool getFlag(const String &key);
    bool setFlag(const String &key, bool value);
    bool exists(const String &key);
    bool remove(const String &key);
    void clear();
    int size() const { return count; }
    void performanceTest();
    // ---- Callback setter ----
    void onConfigSet(ConfigCallback cb) { callback = cb; }

    // ---- Returns all settings as JSON ----
    String getAllSettings(bool previleged = false);
};

/// @brief Global Configs instance (persistent storage)
extern Configs Config;

/// @brief Global System Settings instance (non-persistent storage)
extern Configs SystemSettings;

#endif
#endif