#pragma once

#include <Arduino.h>
#include "NetCodec.h"

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

// NetValueType lives in NetCodec.h, beside the T -> NetValueType mapping.

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

/* Resources are layered as:
 *
 *   application            T
 *                          |  NetValue<T> / NetAction<T>
 *                          v  NetCodec<T>
 *   ResourcesManager / MQTT   String
 *
 * NetResource, NetValueResource and NetActionResource are non-template on
 * purpose: routing, manifests, subscriptions and the transport never see T.
 *
 *   NetResource
 *   |- NetValueResource            non-template boundary
 *   |  '- NetValue<T>              typed implementation
 *   |     |- ManagedSensor<T>      local, READ
 *   |     |- RemoteSensor<T>       remote, READ, STRICT
 *   |     |- ManagedState<T>       local, READ_WRITE
 *   |     '- RemoteState<T>        remote, READ_WRITE, OPTIMISTIC
 *   '- NetActionResource           non-template boundary
 *      '- NetAction<T>             typed payload implementation
 *         |- ManagedAction<T>      this device implements it
 *         '- RemoteAction<T>       another device implements it
 */

struct NetResource
{
    const NetResourceType kind;
    String name;
    NetDeviceIdentity ownerDevice;

    bool isBound() const { return resourceManager != nullptr; }
    bool isOwned() const { return isOwned_; }
    bool setDeviceIdentity(const String &resourceName, const String &resoruceOwner);

    // An empty owner means "this device". The real device name is filled in at
    // bind time, so a globally declared resource never has to resolve the local
    // identity from its constructor.
    NetResource(const String &resourceName, const String &identity, const NetResourceType resourceType)
        : kind(resourceType),
          name(resourceName),
          ownerDevice(identity),
          isOwned_(identity.length() == 0) {};

private:
    bool isOwned_ = false;
    ResourcesManager *resourceManager = nullptr; // Non-owning; set by bindResource().
    friend class ResourcesManager;
    friend struct NetValueResource;
    friend struct NetActionResource;
};

enum class NetSyncStrategy : uint8_t
{
    OPTIMISTIC, // A local write shadows owner state for a short window.
    STRICT,     // Always report the owner's state, even mid-change.
};

/// @brief Manager-facing half of a value resource: everything that does not
/// depend on the value's C++ type, including the optimistic/authoritative state
/// machine. The typed container is NetValue<T>.
struct NetValueResource : public NetResource
{
    // The wire format is String, so this never needs to know T. The
    // application-facing update callback is typed and lives on NetValue<T>.
    using WriteHandler = bool (*)(NetValueResource &resource, const String &requestedValue);

    NetValueResource(const String &resourceName, const String &resourceOwner,
                     AccessPolicy resourceAccess, NetValueType resourceValueType)
        : NetResource(resourceName, resourceOwner, NetResourceType::VALUE),
          access(resourceAccess),
          valueType(resourceValueType) {}
    virtual ~NetValueResource() = default;

    AccessPolicy access = AccessPolicy::READ;
    NetValueType valueType = NetValueType::STRING; // Manifest metadata, from NetCodec<T>::Type.

    WriteHandler onWrite = nullptr; // Owner side: accept or reject a /set request.

    NetSyncStrategy syncStrategy = NetSyncStrategy::OPTIMISTIC;
    uint32_t optimisticWindowMs = 5000; // How long a local write shadows owner state.

    ResourceFreshness freshness = ResourceFreshness::UNKNOWN;
    bool isStale() const { return freshness == ResourceFreshness::STALE; }
    bool hasAuthoritativeValue() const { return hasAuthoritativeValue_; }
    uint32_t lastUpdateMs() const { return lastUpdateMs_; }
    uint32_t lastWriteMs() const { return lastWriteMs_; }

    // Type-erasure boundary. These are the only value operations the transport
    // layer needs, and both speak the encoded wire format.
    virtual String encodedValue() const = 0;
    virtual bool applyEncodedOwnerValue(const String &encoded) = 0;

protected:
    // True while a local write should still shadow owner state.
    bool optimisticActive() const;
    // Access check plus manager dispatch for an application-initiated write.
    bool dispatchLocalWrite(const String &encoded);
    void noteLocalWrite();
    void noteOwnerUpdate();

    bool hasAuthoritativeValue_ = false;
    // Set by the first local write and never cleared: expiry is derived from
    // millis() and lastWriteMs_, so no timer has to reset it.
    bool hasOptimisticValue_ = false;
    uint32_t lastUpdateMs_ = 0;
    uint32_t lastWriteMs_ = 0;

    friend class ResourcesManager;
};

/// @brief The typed value container. Holds the owner's truth and the last
/// locally requested value, and converts at the wire boundary through
/// NetCodec<T>. Deliberately thin: the framework logic lives in the base.
template <typename T>
struct NetValue : public NetValueResource
{
    using UpdateHandler = void (*)(NetValue<T> &resource, const T &value);

    /// @brief A value owned by this device.
    NetValue(const String &resourceName, AccessPolicy resourceAccess = AccessPolicy::READ)
        : NetValueResource(resourceName, String(), resourceAccess, NetCodec<T>::Type) {}

    /// @brief A value owned by another device.
    NetValue(const String &resourceName, const NetDeviceIdentity &owner,
             AccessPolicy resourceAccess = AccessPolicy::READ)
        : NetValueResource(resourceName, owner.deviceName, resourceAccess, NetCodec<T>::Type) {}

    /// @brief Fires when the *effective* value changes, which is what the
    /// application reads. It is not a "packet received" hook: an owner packet
    /// that leaves getValue() unchanged, whether because it repeats the current
    /// value or because an optimistic window is shadowing it, fires nothing.
    UpdateHandler onUpdate = nullptr;

    /// @brief The value the application should act on: the optimistic value
    /// while its window is open, the owner's value otherwise.
    const T &getValue() const
    {
        return optimisticActive() ? optimisticValue_ : authoritativeValue_;
    }

    /// @brief The owner's last reported value, ignoring any optimistic window.
    const T &authoritativeValue() const { return authoritativeValue_; }

    /// @brief Application intent: a local change when owned, a /set request otherwise.
    bool setValue(const T &value)
    {
        const T previous = getValue();
        if (!dispatchLocalWrite(NetCodec<T>::encode(value)))
            return false;

        if (isOwned())
        {
            // This device is the owner, so the request is the new truth.
            authoritativeValue_ = value;
            hasAuthoritativeValue_ = true;
            noteOwnerUpdate();
        }
        else
        {
            optimisticValue_ = value;
            noteLocalWrite();
        }
        notifyIfEffectiveChanged(previous);
        return true;
    }

    /// @brief Framework ingress: the owner reported this value. Refreshes
    /// authoritative state without cancelling an open optimistic window, so a
    /// delayed packet cannot make the application flicker back.
    /// Becomes ResourcesManager-only once the manager pass lands.
    bool applyOwnerValue(const T &value)
    {
        const T previous = getValue();
        authoritativeValue_ = value;
        hasAuthoritativeValue_ = true;
        noteOwnerUpdate();
        notifyIfEffectiveChanged(previous);
        return true;
    }

    String encodedValue() const override { return NetCodec<T>::encode(authoritativeValue_); }

    bool applyEncodedOwnerValue(const String &encoded) override
    {
        T decoded = T();
        if (!NetCodec<T>::decode(encoded, decoded))
            return false;
        return applyOwnerValue(decoded);
    }

private:
    void notifyIfEffectiveChanged(const T &previous)
    {
        if (onUpdate != nullptr && !(getValue() == previous))
            onUpdate(*this, getValue());
    }

    T authoritativeValue_ = T();
    T optimisticValue_ = T();
};

/* The wrappers below only pick ownership, access and sync defaults. They add no
 * fields and no value logic: NetValue<T> stays the implementation, and remains
 * available directly for cases these four do not describe. */

/// @brief Owned by this device, observe-only for everyone else.
template <typename T>
struct ManagedSensor : public NetValue<T>
{
    ManagedSensor(const String &resourceName)
        : NetValue<T>(resourceName, AccessPolicy::READ) {}
};

/// @brief Owned by another device, observe-only. Reports owner state at all
/// times: nothing local can write it, so there is nothing to be optimistic about.
template <typename T>
struct RemoteSensor : public NetValue<T>
{
    RemoteSensor(const String &resourceName, const NetDeviceIdentity &owner)
        : NetValue<T>(resourceName, owner, AccessPolicy::READ)
    {
        this->syncStrategy = NetSyncStrategy::STRICT;
    }
};

/// @brief Owned by this device and writable by others.
template <typename T>
struct ManagedState : public NetValue<T>
{
    ManagedState(const String &resourceName)
        : NetValue<T>(resourceName, AccessPolicy::READ_WRITE) {}
};

/// @brief Owned by another device and writable from here. A local write shows
/// immediately and holds until the owner catches up or the window closes.
template <typename T>
struct RemoteState : public NetValue<T>
{
    RemoteState(const String &resourceName, const NetDeviceIdentity &owner)
        : NetValue<T>(resourceName, owner, AccessPolicy::READ_WRITE)
    {
        this->syncStrategy = NetSyncStrategy::OPTIMISTIC;
    }
};

/// @brief Manager-facing half of an action. v1 is fire-and-forget: no results,
/// no correlation ids, no retries. Those belong with the protocol rewrite.
struct NetActionResource : public NetResource
{
    using InvokeHandler = bool (*)(NetActionResource &resource, const String &payload);

    NetActionResource(const String &resourceName, const String &resourceOwner,
                      NetValueType actionPayloadType)
        : NetResource(resourceName, resourceOwner, NetResourceType::ACTION),
          payloadType(actionPayloadType) {}
    virtual ~NetActionResource() = default;

    // An action exists to be invoked, so READ_WRITE is the meaningful default.
    AccessPolicy access = AccessPolicy::READ_WRITE;
    NetValueType payloadType = NetValueType::NONE; // Manifest metadata, from NetCodec<T>::Type.

    ResourceFreshness freshness = ResourceFreshness::UNKNOWN;
    bool isStale() const { return freshness == ResourceFreshness::STALE; }

    // Type-erasure boundary: Manager -> locally owned action.
    virtual bool applyEncodedInvoke(const String &encoded) = 0;

protected:
    // Application -> remote owner.
    bool dispatchInvoke(const String &encoded);

    friend class ResourcesManager;
};

/// @brief The typed action. Converts the payload through NetCodec<T> and leaves
/// the handler to ManagedAction<T>.
template <typename T>
struct NetAction : public NetActionResource
{
    /// @brief An action implemented by this device.
    NetAction(const String &resourceName)
        : NetActionResource(resourceName, String(), NetCodec<T>::Type) {}

    /// @brief An action implemented by another device.
    NetAction(const String &resourceName, const NetDeviceIdentity &owner)
        : NetActionResource(resourceName, owner.deviceName, NetCodec<T>::Type) {}

    /// @brief Application intent: encode the payload and hand it to the transport.
    bool invoke(const T &payload) { return dispatchInvoke(NetCodec<T>::encode(payload)); }

    bool applyEncodedInvoke(const String &encoded) override
    {
        T payload = T();
        if (!NetCodec<T>::decode(encoded, payload))
            return false;
        return handleDecodedInvoke(payload);
    }

protected:
    /// @brief Where a decoded invoke lands. Only an implementing device has
    /// something to run, so the default refuses.
    virtual bool handleDecodedInvoke(const T &payload)
    {
        (void)payload;
        return false;
    }
};

/// @brief This device implements the action and runs it when invoked.
template <typename T>
struct ManagedAction : public NetAction<T>
{
    using Handler = bool (*)(ManagedAction<T> &action, const T &payload);

    ManagedAction(const String &resourceName) : NetAction<T>(resourceName) {}

    Handler onInvoke = nullptr;

protected:
    bool handleDecodedInvoke(const T &payload) override
    {
        if (onInvoke == nullptr)
            return false;
        return onInvoke(*this, payload);
    }
};

/// @brief Another device implements the action; invoke() sends the request.
template <typename T>
struct RemoteAction : public NetAction<T>
{
    RemoteAction(const String &resourceName, const NetDeviceIdentity &owner)
        : NetAction<T>(resourceName, owner) {}
};
