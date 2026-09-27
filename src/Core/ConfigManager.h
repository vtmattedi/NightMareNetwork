#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>

class ConfigBase;

constexpr size_t ConfigManagerMaxConfigs = 64;
constexpr uint8_t ConfigManifestEncodingVersion = 1;
constexpr uint8_t ConfigManifestVersion = 1;

using ConfigChangeHandler = bool (*)(const String &key, const String &value);

/// @brief Local registry, text ingress and declaration manifest for Config<T>.
/// This manager owns no Config objects and has no transport or persistence role.
class ConfigManager
{
public:
    bool bind(ConfigBase *config);
    bool unbind(ConfigBase *config);

    String handle(const String &command);
    void setChangeHandler(ConfigChangeHandler handler) { changeHandler_ = handler; }

    /// @brief Write the canonical MessagePack manifest into caller storage.
    /// On insufficient capacity, returns false and reports the required byte
    /// count in written without modifying the buffer.
    bool buildManifestMsgPack(uint8_t *buffer, size_t capacity, size_t &written) const;
    String buildManifestBase64() const;

    size_t count() const { return configCount_; }

private:
    ConfigBase *find(const String &name) const;
    bool serializeManifest(String &payload) const;
    String list() const;

    ConfigBase *configs_[ConfigManagerMaxConfigs] = {};
    size_t configCount_ = 0;
    ConfigChangeHandler changeHandler_ = nullptr;
};

/// @brief Process-wide Config registry. Function-local construction makes this
/// safe to use from Config<T> constructors during static initialization.
ConfigManager &configManager();

// Preserve the original public include behavior: callers including only
// ConfigManager.h still receive Config<T>. Include guards make the reciprocal
// include from Config.h safe after ConfigManager is fully declared.
#include "Config.h"
