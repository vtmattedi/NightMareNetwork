#pragma once

#include <Core/Text.h>
#include <Core/PlatformClock.h>
#include <stddef.h>
#include <stdint.h>

class ConfigBase;
template <typename T>
class Config;

constexpr size_t ConfigManagerMaxConfigs = 64;
constexpr uint8_t ConfigManifestEncodingVersion = 1;
constexpr uint8_t ConfigManifestVersion = 1;

using ConfigChangeHandler = bool (*)(const std::string &key, const std::string &value);

/// @brief Persistent registry, text ingress and declaration manifest for Config<T>.
/// This manager owns no Config objects and has no connection role. Persistence
/// is backed by PersistentSettings after the explicit startup restore phase.
class ConfigManager
{
public:
    bool bind(ConfigBase *config);
    bool unbind(ConfigBase *config);

    /// @brief Restores every currently bound Config from PersistentSettings.
    /// Missing or invalid values are replaced with canonical firmware defaults.
    /// Safe to call repeatedly after persistent storage initialization.
    bool restore();
    bool restored() const { return restored_; }

    std::string handle(const std::string &command);
    void setChangeHandler(ConfigChangeHandler handler) { changeHandler_ = handler; }

    /// @brief Write the canonical MessagePack manifest into caller storage.
    /// On insufficient capacity, returns false and reports the required byte
    /// count in written without modifying the buffer.
    bool buildManifestMsgPack(uint8_t *buffer, size_t capacity, size_t &written) const;
    std::string buildManifestBase64() const;

    size_t count() const { return configCount_; }

private:
    ConfigBase *find(const std::string &name) const;
    static std::string storageKey(const std::string &name);
    bool restoreOne(ConfigBase &config);
    bool persist(ConfigBase *config, const std::string &encodedValue);
    bool serializeManifest(std::string &payload) const;
    std::string list() const;

    ConfigBase *configs_[ConfigManagerMaxConfigs] = {};
    size_t configCount_ = 0;
    ConfigChangeHandler changeHandler_ = nullptr;
    bool restored_ = false;

    template <typename T>
    friend class Config;
};

/// @brief Process-wide Config registry. Function-local construction makes this
/// safe to use from Config<T> constructors during static initialization.
ConfigManager &configManager();

// Preserve the original public include behavior: callers including only
// ConfigManager.h still receive Config<T>. Include guards make the reciprocal
// include from Config.h safe after ConfigManager is fully declared.
#include "Config.h"
