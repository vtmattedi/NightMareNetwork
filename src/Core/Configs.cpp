#include "Configs.h"
#ifdef COMPILE_CONFIGS

//
// ────────────────────────────────────────────────────────────────
//   SERIAL LOGGING MACROS (Only active if COMPILE_SERIAL is defined)
// ────────────────────────────────────────────────────────────────
//
#define COMPILE_SERIAL
#ifdef COMPILE_SERIAL
#define CONFIG_TAG "\x1b[36m[CONFIG]\x1b[0m"

#define CONFIG_LOGF(fmt, ...) Serial.printf("%s " fmt "\n", CONFIG_TAG, ##__VA_ARGS__)
#define CONFIG_ERRORF(fmt, ...) Serial.printf("%s %s " fmt "\n", ERR_TAG, CONFIG_TAG, ##__VA_ARGS__)

#else
#define CONFIG_LOGF(fmt, ...)
#define CONFIG_ERRORF(fmt, ...)
#endif

Configs Config;

Configs SystemSettings;

bool Configs::begin()
{
    if (initialized)
    {
        return true;
    }

    if (!LittleFS.begin())
    {
        CONFIG_ERRORF("LittleFS mount failed.");
        SystemSettings.setFlag("LittleFS_mounted", false);
        return false;
    }
    SystemSettings.setFlag("LittleFS_mounted", true);
    CONFIG_LOGF("LittleFS mounted successfully.");
    load();
    initialized = true;
    return true;
}

bool Configs::load()
{
    if (!LittleFS.exists(CONFIGS_FILE))
    {
        CONFIG_LOGF("No config file found, starting blank.");
        clear();
        return save();
    }

    File f = LittleFS.open(CONFIGS_FILE, "r");
    if (!f)
    {
        CONFIG_ERRORF("Failed to open config file.");
        return false;
    }

    StaticJsonDocument<4096> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err)
    {
        // String s = f.readString();
        // Serial.println(s);
        // Serial.println(s.length());
        CONFIG_ERRORF("JSON parse error: %s", err.c_str());
        clear();
        return false;
    }

    clear();

    for (JsonPair kv : doc.as<JsonObject>())
    {
        if (count < CONFIGS_MAX_ENTRIES)
        {
            entries[count].key = kv.key().c_str();
            entries[count].value = kv.value().as<String>();
            count++;
        }
    }

    CONFIG_LOGF("Loaded %d config entries.", count);
    for (int i = 0; i < count; i++)
    {
        CONFIG_LOGF("  %s = %s", entries[i].key.c_str(), entries[i].value.c_str());
    }
    return true;
}

bool Configs::save()
{
    File f = LittleFS.open(CONFIGS_FILE, "w");
    if (!f)
    {
        CONFIG_ERRORF("Failed to open file for writing.");
        return false;
    }

    String json = getAllSettings(true);

    if (f.print(json) == 0)
    {
        CONFIG_ERRORF("Failed to write JSON.");
        f.close();
        return false;
    }

    f.close();
    CONFIG_LOGF("Saved %d entries.", count);
    return true;
}

bool Configs::set(const String &key, const String &value, bool privileged)
{
    if (key.length() == 0)
    {
        CONFIG_ERRORF("Attempted to set empty key.");
        return false;
    }
    for (int i = 0; i < count; i++)
    {
        if (entries[i].key == key)
        {
            // Prevent overwriting privileged keys if not allowed
            // Privileged keys start with an underscore '_' and should be changed be hardcoded code only
            // i.e. wifi credentials should be via WiFi.updateCredentials() not Config.set()
            // this assures a validation step is always done
            if (!privileged && key[0] == '_')
            {
                CONFIG_LOGF("Attempt to set privileged key %s, skipping.", key.c_str());
                return false;
            }
            entries[i].value = value;

            CONFIG_LOGF("Updated %s = %s", key.c_str(), value.c_str());

            if (callback)
                callback(key, value);
            return true;
        }
    }

    if (count >= CONFIGS_MAX_ENTRIES)
    {
        CONFIG_ERRORF("Max entries reached (%d). Cannot set %s.", CONFIGS_MAX_ENTRIES, key.c_str());
        return false;
    }

    entries[count].key = key;
    entries[count].value = value;
    count++;

    CONFIG_LOGF("Added %s = %s", key.c_str(), value.c_str());

    if (callback)
        callback(key, value);

    return true;
}

String Configs::get(const String &key, const String &defaultValue)
{
    for (int i = 0; i < count; i++)
    {
        if (entries[i].key == key)
            return entries[i].value;
    }

    // CONFIG_LOGF("Get %s → default (%s)", key.c_str(), defaultValue.c_str());
    return defaultValue;
}

bool Configs::exists(const String &key)
{
    for (int i = 0; i < count; i++)
        if (entries[i].key == key)
            return true;
    return false;
}

bool Configs::remove(const String &key)
{
    for (int i = 0; i < count; i++)
    {
        if (entries[i].key == key)
        {
            for (int j = i; j < count - 1; j++)
                entries[j] = entries[j + 1];
            count--;

            CONFIG_LOGF("Removed %s", key.c_str());
            return true;
        }
    }

    CONFIG_LOGF("Remove attempted but key not found: %s", key.c_str());
    return false;
}

void Configs::clear()
{
    count = 0;
    CONFIG_LOGF("Cleared all settings.");
}

void Configs::performanceTest()
{
    CONFIG_LOGF("Starting performance test...");

    unsigned long start = millis();

    // Set 100 entries
    for (int i = 0; i < 100; i++)
    {
        String key = "key" + String(i);
        String value = "value" + String(i);
        set(key, value);
    }

    unsigned long setDuration = millis() - start;
    CONFIG_LOGF("Set 100 entries in %lu ms", setDuration);

    start = millis();

    // Get 100 entries
    for (int i = 0; i < 100; i++)
    {
        String key = "key" + String(i);
        get(key);
    }

    unsigned long getDuration = millis() - start;
    CONFIG_LOGF("Got 100 entries in %lu ms", getDuration);
}

bool Configs::setFlag(const String &key, bool value)
{
    return set(key, value ? "1" : "0");
}

bool Configs::getFlag(const String &key)
{
    return get(key, "0") == "1";
}

// ---- Returns all settings as JSON ----
// If privileged is false, keys starting with '_' are skipped
String Configs::getAllSettings(bool privileged)
{
    StaticJsonDocument<4096> doc;

    for (int i = 0; i < count; i++)
    {
        if (!privileged && entries[i].key.length() > 0 && entries[i].key[0] == '_')
            continue;
        doc[entries[i].key] = entries[i].value;
    }

    String out;
    serializeJson(doc, out);

    CONFIG_LOGF("Returning JSON (%d bytes).", out.length());
    return out;
}
#endif