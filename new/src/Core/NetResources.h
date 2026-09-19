#pragma once

#include <Arduino.h>

class ResourcesManager;

constexpr size_t NetResourceMaxPayloadLength = 2048;

enum class NetResourceKind : uint8_t
{
    VALUE,
    ACTION
};

// READ resources can be observed. READ_WRITE values accept /set requests;
// READ_WRITE actions accept /invoke requests.
enum class NetAccess : uint8_t
{
    READ,
    READ_WRITE
};

// Identifies where the authoritative state lives, independent of MQTT broker choice.
enum class ResourceAuthority : uint8_t
{
    THIS_DEVICE,
    OTHER_DEVICE
};

enum class ResourceFreshness : uint8_t
{
    UNKNOWN,
    FRESH,
    STALE
};

// Names are owned by the resource. Namespaces are reserved for a later topic format.
struct NetDeviceIdentity
{
    String deviceName;
    String deviceNamespace;
};

struct NetResource
{
    const NetResourceKind kind;
    String name;
    NetDeviceIdentity ownerDevice;
    NetAccess access = NetAccess::READ;
    ResourceAuthority authority = ResourceAuthority::THIS_DEVICE;

protected:
    explicit NetResource(NetResourceKind resourceKind) : kind(resourceKind) {}

private:
    ResourcesManager *resourceManager = nullptr; // Non-owning; set by bindResource().
    friend class ResourcesManager;
    friend struct NetValueResource;
    friend struct NetActionResource;
};

struct NetValueResource : public NetResource
{
    using WriteHandler = bool (*)(NetValueResource &resource, const String &requestedValue);

    explicit NetValueResource(const String &resourceName = String())
        : NetResource(NetResourceKind::VALUE) { name = resourceName; }

    // THIS_DEVICE: commits and reports state. OTHER_DEVICE: requests a write and waits for /state.
    bool setValue(const String &newValue);
    String getValue() const { return value; }
    bool asBool() const { return value == "true" || value == "1"; }
    int asInt() const { return value.toInt(); }
    float asFloat() const { return value.toFloat(); }

    ResourceFreshness freshness = ResourceFreshness::UNKNOWN;
    // Validate/apply hardware and return true to commit the requested value.
    // Call setValue() outside this handler for independent updates owned by this device.
    WriteHandler onWrite = nullptr;

private:
    String value;
    friend class ResourcesManager;
};

struct NetActionResource : public NetResource
{
    using InvokeHandler = bool (*)(NetActionResource &resource, const String &payload);

    explicit NetActionResource(const String &resourceName = String())
        : NetResource(NetResourceKind::ACTION) { name = resourceName; access = NetAccess::READ_WRITE; }

    bool invoke(const String &payload = String());

    ResourceFreshness freshness = ResourceFreshness::UNKNOWN;
    InvokeHandler onInvoke = nullptr; // Owner-side handler for /invoke requests.
};
