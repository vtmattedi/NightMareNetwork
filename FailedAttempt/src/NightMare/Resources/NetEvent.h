#pragma once
#include <NightMare/Resources/Codec.h>

namespace NightMare {

template<typename Payload = void> class NetEvent final : public NetResource {
public:
    NetEvent(const char* id, const ResourceMetadata* metadata = nullptr)
        : NetResource(id, NetResourceKind::EVENT, NetType<Payload>::valueType, NetAccess::READ, metadata) {}
    bool parse(const String& text, Payload& out) const { return NetCodec<Payload>::decode(text, out); }
    String encode(const Payload& value) const { return NetCodec<Payload>::encode(value); }
};

template<> class NetEvent<void> final : public NetResource {
public:
    NetEvent(const char* id, const ResourceMetadata* metadata = nullptr)
        : NetResource(id, NetResourceKind::EVENT, NetValueType::NONE, NetAccess::READ, metadata) {}
};

} // namespace NightMare
