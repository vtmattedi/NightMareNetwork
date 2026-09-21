#pragma once

#include <Arduino.h>
#include <Core/DeviceIdentity.h>
class ResourcesManager;

constexpr size_t NetResourceMaxPayloadLength = 2048;

enum class NetResourceType : uint8_t
{
    VALUE,
    ACTION
};

// READ resources can be observed. READ_WRITE values accept /set requests;
// READ_WRITE actions accept /invoke requests.
enum class AccessPolicy : uint8_t
{
    READ,
    READ_WRITE
};

enum class ResourceFreshness : uint8_t
{
    UNKNOWN,
    FRESH,
    STALE
};

enum class NetValueType : uint8_t
{
    STRING,
    BOOLEAN,
    INTEGER,
    FLOAT,
    STRUCT
};

// The owner is identified by its device name in the current topic format.
struct NetDeviceIdentity
{
    String deviceName;
    NetDeviceIdentity(const String &name) : deviceName(name) {}
};

/// @brief Resolves a resource topic for a given owner, resource name, resource type, and optional action.
/// @param owner // The owner of the resource, represented by a NetDeviceIdentity object.
/// @param resourceName // The name of the resource for which the topic is being resolved.
/// @param resourceType // The type of the resource, represented by a NetResourceType enum value.
/// @param action // An optional action associated with the resource. Defaults to an empty string if not provided.
/// @return A full topic string in the format "<device>/resources[/<name>/{state,set,invoke}]" based on the provided parameters.
String resolveResourceTopic(const NetDeviceIdentity &owner, const String &resourceName, NetResourceType resourceType, const String &action = String());

/* Now we will have:
 * NetResource: base class for all resources, with common properties like name, owner, access policy, and authority.
 * NetValue: a resource that represents a value.
 * NetAction: a resource that represents an action.
 * OwnedNetValue: a value resource that is owned by the current device.
 * NetWork netValue: a value resource that is not owned by the current device.
 * OwnedNetAction: an action resource that is owned by the current device.
 * NetWork netAction: an action resource that is not owned by the current device.
 */

struct NetResource
{
    const NetResourceType kind;
    String name;
    NetDeviceIdentity ownerDevice;

    bool isBound() const { return resourceManager != nullptr; }
    bool isOwned() const { return isOwned_; }
    bool setDeviceIdentity(const String &resourceName, const String &resoruceOwner);

    NetResource(const String &resourceName, const String &identity, const NetResourceType resourceType) : kind(resourceType),
                                                                                                          name(resourceName),
                                                                                                          ownerDevice(identity) {};

private:
    bool isOwned_ = false;
    ResourcesManager *resourceManager = nullptr; // Non-owning; set by bindResource().
    friend class ResourcesManager;
    friend struct NetValueResource;
    friend struct NetActionResource;
};

struct NetValueResource : public NetResource
{
    using WriteHandler = bool (*)(NetValueResource &resource, const String &requestedValue);
    using StateHandler = void (*)(NetValueResource &resource);

    NetValueResource(const String &resourceName, const String &resourceOwner, AccessPolicy access = AccessPolicy::READ, NetValueType valueType = NetValueType::STRING) : NetResource(resourceName, resourceOwner, NetResourceType::VALUE), valueType(valueType), access(access) {}
    bool setValue(const String &newValue, bool skipManager = false);
    String getValue() const { return value; }
    bool asBool() const { return getValue() == "true" || getValue() == "1"; }
    int asInt() const { return getValue().toInt(); }
    float asFloat() const { return getValue().toFloat(); }
    AccessPolicy access = AccessPolicy::READ; // Access policy of the sensor resource, indicating whether it is read-only or read-write.
    NetValueType valueType = NetValueType::STRING; // just metadata for now.

private:
    String value;
    friend class ResourcesManager;
};

// Owned NetValueResource: a value resource that is owned by the current device.
struct ManagedSensor : public NetValueResource
{
    ManagedSensor(const String &resourceName, AccessPolicy resourceAccess = AccessPolicy::READ, NetValueType valueType = NetValueType::STRING)
        : NetValueResource(resourceName, gDeviceIdentity.getDeviceName(), resourceAccess, valueType) {};
    using WriteHandler = bool (*)(ManagedSensor &resource, const String &requestedValue);
    using StateHandler = void (*)(ManagedSensor &resource);

    WriteHandler onWrite = nullptr; // Owner-side handler for /set requests.
    StateHandler onState = nullptr; // Owner-side handler for state changes.
};

enum class NetSensorSyncStrategy : uint8_t
{
    OPTMISTIC, // Assume that data we wrote is correct until stale or ack.
    STRICT,    // Always show the actual data reported by the sensor, even if it is middle change.
};

/// @brief Represents a sensor resource that is not owned by the current device. It can be used to read values from external sensors and handle changes in their state.
struct NetSensor : public NetValueResource
{
    //Time Allowed for the sensor to adjust its value after a write operation before it is considered stale. This is used in the optimistic sync strategy.
    #define NM_NET_SENSOR_OPTIMISTIC_ADJUST_TIME 5000
    NetSensor(const String &resourceName, const NetDeviceIdentity &owner,
              AccessPolicy resourceAccess = AccessPolicy::READ, NetValueType valueType = NetValueType::STRING) : NetValueResource(resourceName, owner.deviceName, resourceAccess, valueType) {}

    using changeHandler = void (*)(NetSensor &resource, const String &newValue);
    NetSensorSyncStrategy syncStrategy = NetSensorSyncStrategy::OPTMISTIC;
    changeHandler onChange = nullptr;                                      // Handler for value changes detected.
    uint32_t lastUpdateTimestamp = 0;                                      // Timestamp of the last update received from the sensor.
    uint32_t lastWriteTimestamp = 0;                                       // Timestamp of the last write operation performed on the sensor.
    ResourceFreshness freshness = ResourceFreshness::UNKNOWN;              // Freshness state of the sensor's value.
    String optimisticValue;                                                // Holds the last written value when using optimistic sync strategy.
    bool isStale() const { return freshness == ResourceFreshness::STALE; } // Check if the sensor's value is stale.
    bool setValue(const String &newValue, bool isFromOwner = false); // Called by the owner to set the sensor's value and update the timestamp.
    String getValue();
};
