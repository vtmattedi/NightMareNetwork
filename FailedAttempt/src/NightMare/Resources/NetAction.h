#pragma once
#include <NightMare/Resources/Codec.h>

namespace NightMare {

template<typename Args = void> class NetAction final : public NetResource {
public:
    NetAction(const char* id, ActionResponse response = ActionResponse::NONE,
              const ResourceMetadata* metadata = nullptr)
        : NetResource(id, NetResourceKind::ACTION, NetType<Args>::valueType, NetAccess::READ, metadata),
          _response(response) {}
    ActionResponse response() const { return _response; }
    ActionResponse actionResponse() const override { return _response; }
    bool parse(const String& text, Args& out) const { return NetCodec<Args>::decode(text, out); }
    String encode(const Args& value) const { return NetCodec<Args>::encode(value); }
private:
    ActionResponse _response;
};

template<> class NetAction<void> final : public NetResource {
public:
    NetAction(const char* id, ActionResponse response = ActionResponse::NONE,
              const ResourceMetadata* metadata = nullptr)
        : NetResource(id, NetResourceKind::ACTION, NetValueType::NONE, NetAccess::READ, metadata),
          _response(response) {}
    ActionResponse response() const { return _response; }
    ActionResponse actionResponse() const override { return _response; }
private:
    ActionResponse _response;
};

} // namespace NightMare
