#include "ConfigManager.h"

#include "Config.h"
#include "DocumentPayload.h"
#include "PersistentKeys.h"
#include "StateStore.h"
#include "Logs.h"

#include <ArduinoJson.h>
#include <string.h>

static_assert(RuntimeState::MaxEntries >= ConfigManagerMaxConfigs * 2,
              "PersistentSettings must reserve framework headroom beyond Config capacity");

namespace
{
bool commandSpace(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
           value == '\v' || value == '\f';
}

size_t skipCommandSpace(const std::string &text, size_t position)
{
    while (position < text.length() && commandSpace(text[position]))
        ++position;
    return position;
}

std::string commandToken(const std::string &text, size_t &position)
{
    position = skipCommandSpace(text, position);
    const size_t start = position;
    while (position < text.length() && !commandSpace(text[position]))
        ++position;
    return NightMare::Text::slice(text, start, position);
}

bool onlyCommandSpace(const std::string &text, size_t position)
{
    return skipCommandSpace(text, position) == text.length();
}

bool validConfigName(const std::string &name)
{
    if (name.length() == 0)
        return false;
    for (size_t i = 0; i < name.length(); ++i)
    {
        if (commandSpace(name[i]))
            return false;
    }
    return true;
}

const char *valueTypeName(NetValueType type)
{
    switch (type)
    {
    case NetValueType::BOOLEAN:
        return "boolean";
    case NetValueType::INTEGER:
        return "integer";
    case NetValueType::FLOAT:
        return "float";
    case NetValueType::TIME:
        return "time";
    case NetValueType::COLOUR:
        return "colour";
    case NetValueType::STRING:
        return "string";
    case NetValueType::STRUCT:
    default:
        return "struct";
    }
}

std::string encodeBase64(const std::string &input)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const size_t inputLength = input.length();
    const size_t outputLength = ((inputLength + 2) / 3) * 4;
    std::string output;
    if (outputLength != 0 && !NightMare::Text::tryReserve(output, outputLength))
        return std::string();

    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(input.c_str());
    for (size_t i = 0; i < inputLength; i += 3)
    {
        const size_t remaining = inputLength - i;
        const uint32_t block = (static_cast<uint32_t>(bytes[i]) << 16) |
                               (remaining > 1 ? static_cast<uint32_t>(bytes[i + 1]) << 8 : 0) |
                               (remaining > 2 ? static_cast<uint32_t>(bytes[i + 2]) : 0);
        output += alphabet[(block >> 18) & 0x3f];
        output += alphabet[(block >> 12) & 0x3f];
        output += remaining > 1 ? alphabet[(block >> 6) & 0x3f] : '=';
        output += remaining > 2 ? alphabet[block & 0x3f] : '=';
    }
    return output.length() == outputLength ? output : std::string();
}
}

ConfigManager &configManager()
{
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::bind(ConfigBase *config)
{
    if (config == nullptr || !validConfigName(config->name_) ||
        configCount_ >= ConfigManagerMaxConfigs)
        return false;

    for (size_t i = 0; i < configCount_; ++i)
    {
        if (configs_[i] == config || configs_[i]->name_ == config->name_)
            return false;
    }

    configs_[configCount_++] = config;
    config->bound_ = true;
    if (restored_ && !restoreOne(*config))
    {
        configs_[--configCount_] = nullptr;
        config->bound_ = false;
        return false;
    }
    return true;
}

bool ConfigManager::unbind(ConfigBase *config)
{
    if (config == nullptr)
        return false;
    for (size_t i = 0; i < configCount_; ++i)
    {
        if (configs_[i] != config)
            continue;
        for (size_t move = i + 1; move < configCount_; ++move)
            configs_[move - 1] = configs_[move];
        configs_[--configCount_] = nullptr;
        config->bound_ = false;
        return true;
    }
    return false;
}

ConfigBase *ConfigManager::find(const std::string &name) const
{
    for (size_t i = 0; i < configCount_; ++i)
    {
        if (configs_[i]->name_ == name)
            return configs_[i];
    }
    return nullptr;
}

std::string ConfigManager::storageKey(const std::string &name)
{
    return std::string(NightMare::PersistentKey::ConfigPrefix) + name;
}

bool ConfigManager::persist(ConfigBase *config, const std::string &encodedValue)
{
    if (config == nullptr || !config->bound_)
        return false;
    return PersistentSettings.set(storageKey(config->name_), encodedValue);
}

bool ConfigManager::restoreOne(ConfigBase &config)
{
    const std::string key = storageKey(config.name_);
    if (PersistentSettings.exists(key))
    {
        const std::string saved = PersistentSettings.get(key);
        if (config.restoreEncodedValue(saved))
            return true;

        LOG_WARNING("CONFIG", "Invalid persisted value for %s; restoring firmware default",
                    config.name_.c_str());
    }

    const std::string canonicalDefault = config.encodedDefaultValue();
    if (!config.restoreEncodedValue(canonicalDefault))
    {
        config.restoreDefaultValue();
        LOG_ERROR("CONFIG", "Firmware default for %s does not round-trip through its codec",
                  config.name_.c_str());
        return false;
    }
    if (!PersistentSettings.set(key, canonicalDefault))
    {
        LOG_ERROR("CONFIG", "Could not persist firmware default for %s",
                  config.name_.c_str());
        return false;
    }
    return true;
}

bool ConfigManager::restore()
{
    if (restored_)
        return true;
    if (!PersistentSettings.begin())
        return false;

    bool complete = true;
    for (size_t i = 0; i < configCount_; ++i)
        if (!restoreOne(*configs_[i]))
            complete = false;

    restored_ = complete;
    return complete;
}

std::string ConfigManager::list() const
{
    auto displayValue = [](const ConfigBase &config) -> std::string
    {
        std::string value = config.encodedValue();
        NightMare::Text::replaceAll(value, "\r", "\r");
        NightMare::Text::replaceAll(value, "\n", "\n");
        NightMare::Text::replaceAll(value, "\t", "\t");
        return value;
    };

    size_t typeWidth = 4;
    size_t nameWidth = 4;
    for (size_t i = 0; i < configCount_; ++i)
    {
        const ConfigBase &config = *configs_[i];
        const size_t typeLength = strlen(valueTypeName(config.valueType_));
        if (typeLength > typeWidth)
            typeWidth = typeLength;
        if (config.name_.length() > nameWidth)
            nameWidth = config.name_.length();
    }

    std::string output;
    auto appendColumn = [&output](const std::string &text, size_t width)
    {
        output += text;
        for (size_t i = text.length(); i < width; ++i)
            output += ' ';
    };
    appendColumn("TYPE", typeWidth);
    output += "  ";
    appendColumn("NAME", nameWidth);
    output += "  REBOOT  VALUE";
    for (size_t i = 0; i < configCount_; ++i)
    {
        const ConfigBase &config = *configs_[i];
        output += '\n';
        appendColumn(valueTypeName(config.valueType_), typeWidth);
        output += "  ";
        appendColumn(config.name_, nameWidth);
        output += config.requireReboot_ ? "  yes     " : "  -       ";
        output += displayValue(config);
    }
    return output;
}

std::string ConfigManager::listJson() const
{
    JsonDocument doc;
    JsonArray items = doc.to<JsonArray>();
    for (size_t i = 0; i < configCount_; ++i)
    {
        const ConfigBase &config = *configs_[i];
        JsonObject item = items.add<JsonObject>();
        item["name"] = config.name_;
        item["type"] = valueTypeName(config.valueType_);
        item["require_reboot"] = config.requireReboot_;
        item["value"] = config.encodedValue();
    }

    std::string output;
    if (serializeWholeDocument(doc, DocumentFormat::JSON, output) != PayloadResult::Complete)
        return "ERROR: unable to build config list";
    return output;
}

std::string ConfigManager::handle(const std::string &command)
{
    size_t position = 0;
    std::string action = commandToken(command, position);
    NightMare::Text::toLower(action);

    // Same trace as the Resource manager's. A set payload can be a credential, so only
    // the target is logged, never the value.
    if (action == "set" || action == "get")
    {
        size_t preview = position;
        LOG("CM", "Executing command: '%s %s'", action.c_str(), commandToken(command, preview).c_str());
    }
    else
        LOG("CM", "Executing command: '%s'", command.c_str());

    if (action == "list")
    {
        std::string format = commandToken(command, position);
        NightMare::Text::toLower(format);
        if (!onlyCommandSpace(command, position) || (format.length() != 0 && format != "json"))
            return "ERROR: usage: list [json]";
        return format.length() == 0 ? list() : listJson();
    }

    if (action == "manifest")
    {
        if (!onlyCommandSpace(command, position))
            return "ERROR: usage: manifest";
        const std::string encoded = buildManifestBase64();
        return encoded.length() == 0 ? std::string("ERROR: unable to build config manifest") : encoded;
    }

    if (action == "get")
    {
        const std::string name = commandToken(command, position);
        if (name.length() == 0 || !onlyCommandSpace(command, position))
            return "ERROR: usage: get <name>";
        ConfigBase *config = find(name);
        return config == nullptr ? std::string("ERROR: unknown config") : config->encodedValue();
    }

    if (action == "set")
    {
        const std::string name = commandToken(command, position);
        if (name.length() == 0)
            return "ERROR: usage: set <name> <payload>";
        ConfigBase *config = find(name);
        if (config == nullptr)
            return "ERROR: unknown config";

        position = skipCommandSpace(command, position);
        if (position >= command.length())
            return "ERROR: usage: set <name> <payload>";
        const std::string payload = NightMare::Text::slice(command, position);
        if (!config->canDecodeEncodedValue(payload))
            return "ERROR: invalid value";
        if (changeHandler_ != nullptr && !changeHandler_(config->name_, payload))
            return "ERROR: change rejected";
        std::string canonical;
        if (!config->prepareEncodedWrite(payload, canonical))
            return "ERROR: change rejected";
        if (!persist(config, canonical))
            return "ERROR: persistence failed";
        return config->restoreEncodedValue(canonical) ? std::string("OK")
                                                       : std::string("ERROR: invalid value");
    }

    return "ERROR: expected list, get, set, or manifest";
}

bool ConfigManager::serializeManifest(std::string &payload) const
{
    JsonDocument doc;
    JsonArray root = doc.to<JsonArray>();
    root.add(ConfigManifestEncodingVersion);
    root.add(ConfigManifestVersion);
    JsonArray configs = root.add<JsonArray>();
    for (size_t i = 0; i < configCount_; ++i)
    {
        const ConfigBase &config = *configs_[i];
        JsonArray item = configs.add<JsonArray>();
        item.add(config.name_);
        item.add(static_cast<uint8_t>(config.valueType_));
        item.add(config.requireReboot_);
    }
    return serializeWholeDocument(doc, DocumentFormat::MSGPACK, payload) ==
           PayloadResult::Complete;
}

bool ConfigManager::buildManifestMsgPack(uint8_t *buffer, size_t capacity,
                                         size_t &written) const
{
    std::string payload;
    if (!serializeManifest(payload))
    {
        written = 0;
        return false;
    }

    written = payload.length();
    if (buffer == nullptr || capacity < written)
        return false;
    memcpy(buffer, payload.c_str(), written);
    return true;
}

std::string ConfigManager::buildManifestBase64() const
{
    std::string payload;
    if (!serializeManifest(payload))
        return std::string();
    return encodeBase64(payload);
}
