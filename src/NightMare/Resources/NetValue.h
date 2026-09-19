#pragma once
#include <NightMare/Resources/Codec.h>

namespace NightMare {

class NetValueBase : public NetResource {
public:
    NetValueBase(const char* id, NetValueType type, NetAccess access, const ResourceMetadata* metadata)
        : NetResource(id, NetResourceKind::VALUE, type, access, metadata) {}
    virtual String encode() const = 0;
    virtual bool decodeAndSet(const String& text) = 0;
    virtual bool hasValue() const = 0;
};

template<typename T> class NetValue final : public NetValueBase {
public:
    NetValue(const char* id, NetAccess access = NetAccess::READ,
             const ResourceMetadata* metadata = nullptr)
        : NetValueBase(id, NetType<T>::valueType, access, metadata) {}
    const T& get() const { return _value; }
    bool hasValue() const override { return _initialized; }
    bool set(const T& value) {
        if (_initialized && _value == value) return false;
        _value = value;
        _initialized = true;
        return true;
    }
    String encode() const override { return NetCodec<T>::encode(_value); }
    bool decodeAndSet(const String& text) override {
        T value{};
        if (!NetCodec<T>::decode(text, value)) return false;
        set(value);
        return true;
    }
private:
    T _value{};
    bool _initialized = false;
};

} // namespace NightMare
