#include "ConfigManager.h"

#include "DocumentPayload.h"

#include <ArduinoJson.h>
#include <string.h>

namespace
{
bool commandSpace(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
           value == '\v' || value == '\f';
}

size_t skipCommandSpace(const String &text, size_t position)
{
    while (position < text.length() && commandSpace(text[position]))
        ++position;
    return position;
}

String commandToken(const String &text, size_t &position)
{
    position = skipCommandSpace(text, position);
    const size_t start = position;
    while (position < text.length() && !commandSpace(text[position]))
        ++position;
    return text.substring(start, position);
}

bool onlyCommandSpace(const String &text, size_t position)
{
    return skipCommandSpace(text, position) == text.length();
}

bool validConfigName(const String &name)
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
    case NetValueType::STRING:
        return "string";
    case NetValueType::STRUCT:
    default:
        return "struct";
    }
}

String encodeBase64(const String &input)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const size_t inputLength = input.length();
    const size_t outputLength = ((inputLength + 2) / 3) * 4;
    String output;
    if (outputLength != 0 && !output.reserve(outputLength))
        return String();

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
    return output.length() == outputLength ? output : String();
}
}

ConfigManager gConfigManager;

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
        return true;
    }
    return false;
}

ConfigBase *ConfigManager::find(const String &name) const
{
    for (size_t i = 0; i < configCount_; ++i)
    {
        if (configs_[i]->name_ == name)
            return configs_[i];
    }
    return nullptr;
}

String ConfigManager::list() const
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
    }

    String output;
    if (serializeWholeDocument(doc, DocumentEncoding::JSON, output) != PayloadResult::Complete)
        return "ERROR: unable to build config list";
    return output;
}

String ConfigManager::handle(const String &command)
{
    size_t position = 0;
    String action = commandToken(command, position);
    action.toLowerCase();

    if (action == "list")
    {
        if (!onlyCommandSpace(command, position))
            return "ERROR: usage: list";
        return list();
    }

    if (action == "manifest")
    {
        if (!onlyCommandSpace(command, position))
            return "ERROR: usage: manifest";
        const String encoded = buildManifestBase64();
        return encoded.length() == 0 ? String("ERROR: unable to build config manifest") : encoded;
    }

    if (action == "get")
    {
        const String name = commandToken(command, position);
        if (name.length() == 0 || !onlyCommandSpace(command, position))
            return "ERROR: usage: get <name>";
        ConfigBase *config = find(name);
        return config == nullptr ? String("ERROR: unknown config") : config->encodedValue();
    }

    if (action == "set")
    {
        const String name = commandToken(command, position);
        if (name.length() == 0)
            return "ERROR: usage: set <name> <payload>";
        ConfigBase *config = find(name);
        if (config == nullptr)
            return "ERROR: unknown config";

        position = skipCommandSpace(command, position);
        if (position >= command.length())
            return "ERROR: usage: set <name> <payload>";
        const String payload = command.substring(position);
        if (!config->canDecodeEncodedValue(payload))
            return "ERROR: invalid value";
        if (changeHandler_ != nullptr && !changeHandler_(config->name_, payload))
            return "ERROR: change rejected";
        return config->applyEncodedValue(payload) ? String("OK") : String("ERROR: invalid value");
    }

    return "ERROR: expected list, get, set, or manifest";
}

bool ConfigManager::serializeManifest(String &payload) const
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
    return serializeWholeDocument(doc, DocumentEncoding::MSGPACK, payload) ==
           PayloadResult::Complete;
}

bool ConfigManager::buildManifestMsgPack(uint8_t *buffer, size_t capacity,
                                         size_t &written) const
{
    String payload;
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

String ConfigManager::buildManifestBase64() const
{
    String payload;
    if (!serializeManifest(payload))
        return String();
    return encodeBase64(payload);
}
