#pragma once
#include <Arduino.h>
#include <stdint.h>

namespace NightMare {

enum class NetResourceKind : uint8_t { VALUE, ACTION, EVENT };
enum class NetValueType : uint8_t {
    NONE, BOOL, INT8, UINT8, INT16, UINT16, INT32, UINT32,
    INT64, UINT64, FLOAT32, FLOAT64, STRING, STRUCT
};
enum class NetAccess : uint8_t { READ, READ_WRITE };
enum class ResourceRole : uint8_t { LOCAL, REMOTE };
enum class ResourceFreshness : uint8_t { UNKNOWN, FRESH, STALE };
enum class ActionResponse : uint8_t { NONE, ACK, RESULT };
enum class ActionStatus : uint8_t { OK, REJECTED, INVALID_ARGUMENT, BUSY, ERROR };

// Optional, caller-owned metadata. A resource costs one pointer whether or not
// the application supplies labels, units or bounds.
struct ResourceMetadata {
    const char* label = nullptr;
    const char* unit = nullptr;
    const char* description = nullptr;
    const char* group = nullptr;
    const char* minimum = nullptr;
    const char* maximum = nullptr;
    struct Field { const char* id; NetValueType type; };
    const Field* fields = nullptr;
    uint8_t fieldCount = 0;
};

class NetResource {
public:
    NetResource(const char* id, NetResourceKind kind, NetValueType type,
                NetAccess access = NetAccess::READ, const ResourceMetadata* metadata = nullptr)
        : _id(id), _kind(kind), _type(type), _access(access), _metadata(metadata) {}
    virtual ~NetResource() = default;
    const String& id() const { return _id; }
    NetResourceKind kind() const { return _kind; }
    NetValueType type() const { return _type; }
    NetAccess access() const { return _access; }
    const ResourceMetadata* metadata() const { return _metadata; }
    virtual ActionResponse actionResponse() const { return ActionResponse::NONE; }

private:
    String _id;
    NetResourceKind _kind;
    NetValueType _type;
    NetAccess _access;
    const ResourceMetadata* _metadata;
};

// Namespace is logical identity; cluster/broker selection is transport policy.
struct ResourceAddress {
    String nameSpace;
    String device;
    String resource;
};

} // namespace NightMare
