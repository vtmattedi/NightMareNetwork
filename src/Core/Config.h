#pragma once

#include "NetCodec.h"

class ConfigManager;

/// @brief Non-template metadata and command-ingress boundary for a Config<T>.
/// Config objects are application-owned and must outlive their manager binding.
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

private:
    virtual String encodedValue() const = 0;
    virtual bool canDecodeEncodedValue(const String &payload) const = 0;
    virtual bool applyEncodedValue(const String &payload) = 0;

    String name_;
    bool requireReboot_ = false;
    NetValueType valueType_ = NetValueType::STRING;

    friend class ConfigManager;
};

/// @brief A firmware-declared, typed local configuration value.
/// Local set() calls are direct assignments. Only ConfigManager command ingress
/// invokes the optional global change handler.
template <typename T>
class Config : public ConfigBase
{
public:
    explicit Config(const String &name, bool requireReboot = false)
        : ConfigBase(name, requireReboot, NetCodec<T>::Type)
    {
    }

    const T &value() const { return value_; }

    bool set(const T &value)
    {
        value_ = value;
        return true;
    }

private:
    String encodedValue() const override { return NetCodec<T>::encode(value_); }

    bool canDecodeEncodedValue(const String &payload) const override
    {
        T decoded{};
        return NetCodec<T>::decode(payload, decoded);
    }

    bool applyEncodedValue(const String &payload) override
    {
        T decoded{};
        if (!NetCodec<T>::decode(payload, decoded))
            return false;
        value_ = decoded;
        return true;
    }

    T value_{};
};
