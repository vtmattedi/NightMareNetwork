#include <NightMare/Features.h>
#if NM_ENABLE_SETTINGS

#include "StateStore.h"
#include "SystemState.h"
#include "Storage.h"
#include <ArduinoJson.h>

namespace
{
constexpr size_t MaxSettingsBytes = 32768;
}

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
    if (!NightMare::Storage::mount())
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
    if (!NightMare::Storage::exists(SETTINGS_FILE))
    {
        RuntimeState::clear();
        return save();
    }

    std::string file;
    if (!NightMare::Storage::read(SETTINGS_FILE, file, MaxSettingsBytes))
        return false;
    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, file);
    if (error || !doc.is<JsonObject>())
        return false;

    RuntimeState::clear();
    for (JsonPair item : doc.as<JsonObject>())
    {
        if (!RuntimeState::set(item.key().c_str(), item.value().as<std::string>()))
            return false;
    }
    return true;
}

bool StateStore::save()
{
    if (!persistent_)
        return false;
    const std::string json = RuntimeState::toJson();

    return json.size() <= MaxSettingsBytes &&
           NightMare::Storage::write(SETTINGS_FILE, json, true);
}

bool StateStore::set(const std::string &key, const std::string &value)
{
    if (!begin())
        return false;

    const bool existed = RuntimeState::exists(key);
    const std::string previous = existed ? RuntimeState::get(key) : std::string();
    if (!RuntimeState::set(key, value))
        return false;
    if (!persistent_ || save())
        return true;

    if (existed)
        RuntimeState::set(key, previous);
    else
        RuntimeState::remove(key);
    return false;
}

bool StateStore::setMany(const std::string *keys, const std::string *values, size_t count)
{
    if (!begin())
        return false;
    for (size_t i = 0; i < count; ++i)
        if (!RuntimeState::set(keys[i], values[i]))
            return false;
    return !persistent_ || save();
}

std::string StateStore::get(const std::string &key, const std::string &defaultValue) const
{
    return const_cast<StateStore *>(this)->begin()
               ? RuntimeState::get(key, defaultValue) : defaultValue;
}

std::string StateStore::getOrSave(const std::string &key, const std::string &defaultValue) const
{
    if (!const_cast<StateStore *>(this)->begin())
        return defaultValue;
    if (RuntimeState::exists(key))
        return RuntimeState::get(key, defaultValue);
    const_cast<StateStore *>(this)->set(key, defaultValue);
    return defaultValue;
}

bool StateStore::exists(const std::string &key) const
{
    return const_cast<StateStore *>(this)->begin() && RuntimeState::exists(key);
}

bool StateStore::remove(const std::string &key)
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

std::string StateStore::toJson() const
{
    return const_cast<StateStore *>(this)->begin()
               ? RuntimeState::toJson() : std::string("{}");
}

#endif // NM_ENABLE_SETTINGS
