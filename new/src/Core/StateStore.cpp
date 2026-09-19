#include "StateStore.h"
char DEVICE_NAME[DEVICE_NAME_MAX] = "Esp32-nm-"; // Default device name used by MQTT and other modules
void initPersistentSettings(StateStore &settings);
StateStore PersistentSettings(true); // Global instance of StateStore with auto-save enabled

StateStore SystemState(false); // Global instance of StateStore for non-persistent system settings with auto-save disabled

StateStore::StateStore(bool persistent)
{
    persistent_ = persistent;
}

bool StateStore::begin()
{
    if (initialized)
    {
        return true;
    }

    if (!persistent_)
    {
        initialized = true;
        return true;
    }

    if (!LittleFS.begin(true)) // format-on-fail: formats a fresh/corrupt FS partition once, then mounts
    {
        SystemState.setFlag("LittleFS_mounted", false);
        return false;
    }
    SystemState.setFlag("LittleFS_mounted", true);
    load();
    initialized = true;
    initPersistentSettings(*this);

    return true;
}

bool StateStore::load()
{
    if (!persistent_)
        return false;

    if (!LittleFS.exists(SETTINGS_FILE))
    {
        clear();
        return save();
    }

    File f = LittleFS.open(SETTINGS_FILE, "r");
    if (!f)
    {
        return false;
    }

    StaticJsonDocument<4096> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err)
    {
        clear();
        return false;
    }

    clear();

    for (JsonPair kv : doc.as<JsonObject>())
    {
        if (count < STATE_STORE_MAX_ENTRIES)
        {
            entries[count].key = kv.key().c_str();
            entries[count].value = kv.value().as<String>();
            count++;
        }
    }

    return true;
}

bool StateStore::save()
{
    if (!persistent_)
        return false;

    File f = LittleFS.open(SETTINGS_FILE, "w");
    if (!f)
    {
        return false;
    }

    String json = toJson();

    if (f.print(json) == 0)
    {
        f.close();
        return false;
    }

    f.close();
    return true;
}

bool StateStore::set(const String &key, const String &value)
{
    // no key
    if (key.length() == 0)
    {
        return false;
    }

    // key already exists, update value
    for (int i = 0; i < count; i++)
    {
        if (entries[i].key == key)
        {
            entries[i].value = value;


            if (persistent_)
                save();
            return true;
        }
    }

    // key does not exist -> settings are full
    if (count >= STATE_STORE_MAX_ENTRIES)
    {
        return false;
    }

    // Add new key-value pair
    entries[count].key = key;
    entries[count].value = value;
    count++;


    if (persistent_)
        save();

    return true;
}

String StateStore::get(const String &key, const String &defaultValue)
{
    if (!initialized && persistent_) // Load persistent settings on first access.
    {
        bool res = begin();
        if (!res)
        {
            return defaultValue;
        }
    }
    for (int i = 0; i < count; i++)
    {
        if (entries[i].key == key)
        {
            return entries[i].value;
        }
    }

    return defaultValue;
}

bool StateStore::exists(const String &key)
{
    if (key.length() == 0)
    {
        return false;
    }
    for (int i = 0; i < count; i++)
        if (entries[i].key == key)
            return true;
    return false;
}

bool StateStore::remove(const String &key)
{
    for (int i = 0; i < count; i++)
    {
        if (entries[i].key == key)
        {
            for (int j = i; j < count - 1; j++)
                entries[j] = entries[j + 1];
            count--;

            if (persistent_)
                save();
            return true;
        }
    }

    return false;
}

void StateStore::clear(bool saveChanges)
{
    count = 0;
    for (int i = 0; i < STATE_STORE_MAX_ENTRIES; i++)
    {
        entries[i].key = "";
        entries[i].value = "";
    }
    if (saveChanges && persistent_)
    {
        save();
    }
}

bool StateStore::setFlag(const String &key, bool value)
{
    return set(key, value ? "1" : "0");
}

bool StateStore::getFlag(const String &key)
{
    return get(key, "0") == "1";
}

void initPersistentSettings(StateStore &settings)
{
    if (!settings.exists("_device_name"))
    {
        String defaultName = String(DEVICE_NAME) + String((uint32_t)ESP.getEfuseMac(), HEX);
        settings.set("_device_name", defaultName);
    }
    String name = settings.get("_device_name");
    strncpy(DEVICE_NAME, name.c_str(), DEVICE_NAME_MAX - 1);
    DEVICE_NAME[DEVICE_NAME_MAX - 1] = '\0';
}

const char *getDeviceName()
{
    if (DEVICE_NAME[0] == '\0')
    {
        return "ESP32-Device";
    }
    return DEVICE_NAME;
}

// ---- Returns all settings as JSON ----
String StateStore::toJson()
{
    StaticJsonDocument<4096> doc;

    for (int i = 0; i < count; i++)
    {
        doc[entries[i].key] = entries[i].value;
    }

    String out;
    serializeJson(doc, out);

    return out;
}
