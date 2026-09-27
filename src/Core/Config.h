#pragma once

#include "ConfigManager.h"
#include "NetCodec.h"

/// @brief Non-template metadata and command-ingress boundary for a Config<T>.
/// Config objects are application-owned and unregister when destroyed.
class ConfigBase
{
public:
    virtual ~ConfigBase() = default;

    const String &name() const { return name_; }
    bool requiresReboot() const { return requireReboot_; }
    NetValueType type() const { return valueType_; }

protected:
    ConfigBase(const String &name, bool requireReboot, NetValueType valueType)
        : name_(name), requireReboot_(requireReboot), valueType_(valueType)
    {
    }

    bool bound_ = false;

private:
    virtual String encodedValue() const = 0;
    virtual String encodedDefaultValue() const = 0;
    virtual bool canDecodeEncodedValue(const String &payload) const = 0;
    virtual bool prepareEncodedWrite(const String &payload, String &canonical) = 0;
    virtual bool restoreEncodedValue(const String &payload) = 0;
    virtual void restoreDefaultValue() = 0;

    String name_;
    bool requireReboot_ = false;
    NetValueType valueType_ = NetValueType::STRING;
    friend class ConfigManager;
};

/// @brief A firmware-declared, typed persistent configuration value.
/// Construction registers with configManager(); destruction unregisters.
/// Local set() calls persist without invoking ingress handlers. ConfigManager
/// command ingress invokes the optional global and per-Config write handlers
/// before persisting and committing.
/// Naming convention: prefer "<group>:<value>" or deeper colon-separated
/// groups when related declarations naturally belong together. Colons remain
/// ordinary name characters; the convention is not enforced.
template <typename T>
class Config : public ConfigBase
{
public:
    /// @brief Returns true to accept an already-decoded ConfigManager write.
    /// Returning false leaves the current value unchanged.
    using WriteRequestHandler = bool (*)(Config<T> &config, const T &requested);

    Config(const String &name, const T &defaultValue, bool requireReboot = false)
        : ConfigBase(name, requireReboot, NetCodec<T>::Type),
          defaultValue_(defaultValue), value_(defaultValue)
    {
        bound_ = configManager().bind(this);
    }

    ~Config() override
    {
        if (bound_)
            configManager().unbind(this);
    }

    const T &value() const { return value_; }

    WriteRequestHandler onWrite = nullptr;

    bool set(const T &value)
    {
        if (!configManager().persist(this, NetCodec<T>::encode(value)))
            return false;
        value_ = value;
        return true;
    }

private:
    String encodedValue() const override { return NetCodec<T>::encode(value_); }
    String encodedDefaultValue() const override { return NetCodec<T>::encode(defaultValue_); }

    bool canDecodeEncodedValue(const String &payload) const override
    {
        T decoded{};
        return NetCodec<T>::decode(payload, decoded);
    }

    bool prepareEncodedWrite(const String &payload, String &canonical) override
    {
        T decoded{};
        if (!NetCodec<T>::decode(payload, decoded))
            return false;
        if (onWrite != nullptr && !onWrite(*this, decoded))
            return false;
        canonical = NetCodec<T>::encode(decoded);
        return true;
    }

    bool restoreEncodedValue(const String &payload) override
    {
        T decoded{};
        if (!NetCodec<T>::decode(payload, decoded))
            return false;
        value_ = decoded;
        return true;
    }

    void restoreDefaultValue() override { value_ = defaultValue_; }

    T defaultValue_{};
    T value_{};
};
