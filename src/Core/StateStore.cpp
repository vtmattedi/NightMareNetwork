#include <NightMare/Features.h>
#if NM_ENABLE_SETTINGS

#include "StateStore.h"
#include "SystemState.h"
#include <ArduinoJson.h>
#include <LittleFS.h>

StateStore PersistentSettings(true);

bool StateStore::begin()
{
    if (initialized_)
        return true;
    if (!persistent_)
    {
        initialized_ = true;
        return true;
    }
    if (!LittleFS.begin(true))
    {
        SystemState.clear(SystemFlag::PersistentStorageReady);
        return false;
    }
    SystemState.set(SystemFlag::PersistentStorageReady);
    if (!load())
        RuntimeState::clear(); // Leave invalid files untouched until settings are written.
    initialized_ = true;
    return true;
}

bool StateStore::load()
{
    if (!persistent_)
        return false;
    if (!LittleFS.exists(SETTINGS_FILE))
    {
        RuntimeState::clear();
        return save();
    }

    File file = LittleFS.open(SETTINGS_FILE, "r");
    if (!file)
        return false;
    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error || !doc.is<JsonObject>())
        return false;

    RuntimeState::clear();
    for (JsonPair item : doc.as<JsonObject>())
    {
        if (!RuntimeState::set(item.key().c_str(), item.value().as<String>()))
            return false;
    }
    return true;
}

bool StateStore::save()
{
    if (!persistent_)
        return false;
    const String json = RuntimeState::toJson();
    File file = LittleFS.open(SETTINGS_FILE, "w");
    if (!file)
        return false;
    const size_t written = file.print(json);
    file.close();
    return written == json.length();
}

bool StateStore::set(const String &key, const String &value)
{
    if (!begin() || !RuntimeState::set(key, value))
        return false;
    return !persistent_ || save();
}

String StateStore::get(const String &key, const String &defaultValue) const
{
    return const_cast<StateStore *>(this)->begin()
               ? RuntimeState::get(key, defaultValue) : defaultValue;
}

String StateStore::getOrSave(const String &key, const String &defaultValue) const
{
    if (!const_cast<StateStore *>(this)->begin())
        return defaultValue;
    if (RuntimeState::exists(key))
        return RuntimeState::get(key, defaultValue);
    const_cast<StateStore *>(this)->set(key, defaultValue);
    return defaultValue;
}

bool StateStore::exists(const String &key) const
{
    return const_cast<StateStore *>(this)->begin() && RuntimeState::exists(key);
}

bool StateStore::remove(const String &key)
{
    if (!begin() || !RuntimeState::remove(key))
        return false;
    return !persistent_ || save();
}

void StateStore::clear()
{
    if (!begin())
        return;
    RuntimeState::clear();
    if (persistent_)
        save();
}

void StateStore::clear(bool saveChanges)
{
    if (saveChanges)
    {
        clear();
        return;
    }
    if (begin())
        RuntimeState::clear();
}

size_t StateStore::size() const
{
    return const_cast<StateStore *>(this)->begin() ? RuntimeState::size() : 0;
}

String StateStore::toJson() const
{
    return const_cast<StateStore *>(this)->begin()
               ? RuntimeState::toJson() : String("{}");
}

#endif // NM_ENABLE_SETTINGS
