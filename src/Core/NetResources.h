#pragma once

#include <Arduino.h>
#include "NetCodec.h"

class ResourcesManager;

constexpr size_t NetResourceMaxPayloadLength = 2048;
constexpr size_t NetResourceMaxManifestLength = 16384;
constexpr size_t NetResourceMaxCommandLength = NetResourceMaxManifestLength + 256;

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

/* Resources are layered as:
 *
 *   application            T
 *                          |  NetValue<T>
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
 *   '- NetActionResource           non-template, runtime argument metadata
 *      |- ManagedAction            local implementation + arg schema
 *      '- RemoteAction             remote invocation + arg schema
 *
 * Sensor<T> observes T. State<T> observes T and can request it become another
 * value. An action performs an operation described by runtime metadata, so it
 * has no single T and is not templated.
 */

/// @brief Whether this device implements the resource or merely points at one.
/// Fixed when the resource is declared: retargeting a Remote resource changes
/// what it points at, never what it is.
enum class ResourceRole : uint8_t
{
    MANAGED, // Implemented here.
    REMOTE   // Implemented by another device.
};

struct NetResource
{
    const NetResourceType kind;
    String name;
    NetDeviceIdentity ownerDevice;

    bool isBound() const { return resourceManager != nullptr; }
    bool isOwned() const { return role_ == ResourceRole::MANAGED; }

    // A MANAGED resource leaves ownerDevice empty for good: its owner is always
    // the current device identity, read when a topic is resolved (see
    // resolveResourceOwner). Nothing is copied at construction or bind time, so
    // a global declaration never has to know the local name. A REMOTE one may
    // also start empty and be pointed at a source later with setSource().
    NetResource(const String &resourceName, const String &identity,
                const NetResourceType resourceType, const ResourceRole resourceRole)
        : kind(resourceType),
          name(resourceName),
          ownerDevice(identity),
          role_(resourceRole) {};
    virtual ~NetResource() = default;

protected:
    /// @brief Points a REMOTE resource at a different source. Updates the target
    /// only: the role is permanent, so ownership is never recalculated here.
    void setRemoteSource(const String &deviceName, const String &resourceName);

    /// @brief Drops whatever was learned from the previous source.
    virtual void resetRemoteState() {}

private:
    const ResourceRole role_;
    ResourcesManager *resourceManager = nullptr; // Non-owning; set by bindResource().
    friend class ResourcesManager;
    friend struct NetValueResource;
    friend struct NetActionResource;
};

/* Canonical addressing. Topics belong to the resource layer, so a resource can
 * name itself without a ResourcesManager, and the Manager builds nothing of its
 * own. These only assemble strings: callers validate the segments first.
 *
 *   <device>/resources                  manifest, retained
 *   <device>/resources/<name>/state     value state, retained
 *   <device>/resources/<name>/set       write request, transient
 *   <device>/resources/<name>/invoke    action request, transient
 */
enum class ResourceTopicOperation : uint8_t
{
    STATE,
    SET,
    INVOKE
};

/// @brief The device that implements the resource: the current identity for a
/// MANAGED one, the configured source for a REMOTE one. A reference, because
/// both live as long as the resource; the Manager calls this per registry entry.
const String &resolveResourceOwner(const NetResource &resource);

String resolveResourceTopic(const NetResource &resource, ResourceTopicOperation operation);

/// @brief Explicit address, for topics that are not the resource's current one,
/// such as a previous identity being cleaned up.
String resolveResourceTopic(const String &deviceName, const String &resourceName,
                            ResourceTopicOperation operation);

String resolveResourceManifestTopic(const String &deviceName);

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
    NetValueResource(const String &resourceName, const String &resourceOwner,
                     AccessPolicy resourceAccess, NetValueType resourceValueType,
                     ResourceRole resourceRole)
        : NetResource(resourceName, resourceOwner, NetResourceType::VALUE, resourceRole),
          access(resourceAccess),
          valueType(resourceValueType) {}

    AccessPolicy access = AccessPolicy::READ;
    NetValueType valueType = NetValueType::STRING; // Manifest metadata, from NetCodec<T>::Type.

    // Optimism is opt-in: only RemoteState<T> selects it, because only a write
    // that has to travel to another device has a gap worth papering over.
    NetSyncStrategy syncStrategy = NetSyncStrategy::STRICT;
    uint32_t optimisticWindowMs = 5000; // How long a local write shadows owner state.

    ResourceFreshness freshness = ResourceFreshness::UNKNOWN;
    bool isStale() const { return freshness == ResourceFreshness::STALE; }
    bool hasAuthoritativeValue() const { return hasAuthoritativeValue_; }
    bool hasCurrentValue() const { return hasAuthoritativeValue_ || optimisticActive(); }
    uint32_t lastUpdateMs() const { return lastUpdateMs_; }
    uint32_t lastWriteMs() const { return lastWriteMs_; }

    // Type-erasure boundary. These are the only value operations the transport
    // layer needs, and all three speak the encoded wire format.
    virtual String encodedValue() const = 0;
    virtual String encodedCurrentValue() const = 0;
    // A local caller requests a value using its wire representation. Remote
    // values retain their normal optimistic-write behaviour through this path.
    virtual bool requestEncodedValue(const String &encoded) = 0;
    // Ingress of owner state: this value is now the truth.
    virtual bool applyEncodedOwnerValue(const String &encoded) = 0;
    // Ingress of a /set request aimed at a value this device owns.
    virtual bool applyEncodedWrite(const String &encoded) = 0;

protected:
    // Everything learned from a source is per-source, so retargeting clears it.
    void resetRemoteState() override;

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
        : NetValueResource(resourceName, String(), resourceAccess, NetCodec<T>::Type,
                           ResourceRole::MANAGED) {}

    /// @brief A value owned by another device.
    NetValue(const String &resourceName, const NetDeviceIdentity &owner,
             AccessPolicy resourceAccess = AccessPolicy::READ)
        : NetValueResource(resourceName, owner.deviceName, resourceAccess, NetCodec<T>::Type,
                           ResourceRole::REMOTE) {}

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
    String encodedCurrentValue() const override { return NetCodec<T>::encode(getValue()); }

    bool requestEncodedValue(const String &encoded) override
    {
        T requested = T();
        if (!NetCodec<T>::decode(encoded, requested))
            return false;
        return setValue(requested);
    }

    bool applyEncodedOwnerValue(const String &encoded) override
    {
        T decoded = T();
        if (!NetCodec<T>::decode(encoded, decoded))
            return false;
        return applyOwnerValue(decoded);
    }

    bool applyEncodedWrite(const String &encoded) override
    {
        T requested = T();
        if (!NetCodec<T>::decode(encoded, requested))
            return false;
        return handleDecodedWrite(requested);
    }

protected:
    /// @brief Where a decoded /set request lands. Only a device that owns the
    /// value and offers a handler has something to decide, so the default
    /// refuses. ManagedState<T> overrides this.
    virtual bool handleDecodedWrite(const T &requested)
    {
        (void)requested;
        return false;
    }

    /// @brief The stored values belong to the old source too, so they go with
    /// it rather than lingering as a readable reading from the wrong device.
    void resetRemoteState() override
    {
        NetValueResource::resetRemoteState();
        authoritativeValue_ = T();
        optimisticValue_ = T();
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
struct  RemoteSensor : public NetValue<T>
{
    /// @brief Declared without a source yet; point it at one with setSource().
    RemoteSensor() : NetValue<T>(String(), NetDeviceIdentity(String()), AccessPolicy::READ)
    {
        this->syncStrategy = NetSyncStrategy::STRICT;
    }

    RemoteSensor(const String &resourceName, const NetDeviceIdentity &owner)
        : NetValue<T>(resourceName, owner, AccessPolicy::READ)
    {
        this->syncStrategy = NetSyncStrategy::STRICT;
    }

    /// @brief Points this at a different remote resource. Stays REMOTE whatever
    /// device is named, and drops everything learned from the old source.
    void setSource(const String &deviceName, const String &resourceName)
    {
        this->setRemoteSource(deviceName, resourceName);
    }
};

/// @brief Owned by this device and writable by others. The handler receives the
/// already-decoded value: String conversion stops at the resource boundary.
template <typename T>
struct ManagedState : public NetValue<T>
{
    /// @brief Returns true to accept the request, which then becomes the
    /// authoritative value, or false to reject it and leave state untouched.
    using WriteRequestHandler = bool (*)(ManagedState<T> &state, const T &requested);

    ManagedState(const String &resourceName)
        : NetValue<T>(resourceName, AccessPolicy::READ_WRITE) {}

    WriteRequestHandler onWrite = nullptr;

protected:
    bool handleDecodedWrite(const T &requested) override
    {
        if (onWrite == nullptr || !onWrite(*this, requested))
            return false;
        // Accepted by the owner, so the request is the new truth.
        this->applyOwnerValue(requested);
        return true;
    }
};

/// @brief Owned by another device and writable from here. A local write shows
/// immediately and holds until the owner catches up or the window closes.
template <typename T>
struct RemoteState : public NetValue<T>
{
    /// @brief Declared without a source yet; point it at one with setSource().
    RemoteState() : NetValue<T>(String(), NetDeviceIdentity(String()), AccessPolicy::READ_WRITE)
    {
        this->syncStrategy = NetSyncStrategy::OPTIMISTIC;
    }

    RemoteState(const String &resourceName, const NetDeviceIdentity &owner)
        : NetValue<T>(resourceName, owner, AccessPolicy::READ_WRITE)
    {
        this->syncStrategy = NetSyncStrategy::OPTIMISTIC;
    }

    /// @brief Points this at a different remote resource. Stays REMOTE whatever
    /// device is named, and drops everything learned from the old source,
    /// including any optimistic window still open against it.
    void setSource(const String &deviceName, const String &resourceName)
    {
        this->setRemoteSource(deviceName, resourceName);
    }
};

/// @brief Describes one action argument. Actions carry a runtime schema rather
/// than a single payload type: an operation's arguments have nothing to do with
/// each other, so there is no one T to template on.
struct ActionArgMetadata
{
    const char *name;
    NetValueType type;
    // Optional arguments let a declaration grow without breaking older callers.
    // What an omitted one means is the handler's decision; there are no defaults.
    bool required;

    // A constructor rather than a default member initializer: under C++11 the
    // latter would stop this being an aggregate and break the brace syntax.
    constexpr ActionArgMetadata(const char *argName, NetValueType argType,
                                bool argRequired = true)
        : name(argName), type(argType), required(argRequired) {}
};

/// @brief What a local implementation reports back. An ordinary MQTT /invoke
/// discards it; a controlled console or MQTTP path can surface it. There is
/// deliberately no result topic, request id or retained execution state.
struct ActionResult
{
    bool success;
    String result;
};

/// @brief Manager-facing action. Holds the argument schema and nothing else: no
/// payload syntax, no freshness, no value semantics. Turning MQTT JSON or
/// positional console input into arguments is the Manager's job, so the
/// execution boundary here stays a plain encoded String.
///
/// The schema is self-description first: it goes into the manifest for
/// discovery, documentation and tooling. Checking payloads against it is an
/// optional extra (NM_ENABLE_ACTION_PAYLOAD_ASSERTION), and it is tolerant:
/// unknown fields pass, so either side can grow new optional arguments. A
/// change old callers cannot survive deserves a new action name, not a check.
struct NetActionResource : public NetResource
{
    /// @param args Not copied. The schema has to outlive the action, so a static
    /// or global array is the expected source.
    NetActionResource(const String &resourceName, const String &resourceOwner,
                      const ActionArgMetadata *args, size_t argCount,
                      ResourceRole resourceRole)
        : NetResource(resourceName, resourceOwner, NetResourceType::ACTION, resourceRole),
          arguments_(args),
          argumentCount_(argCount) {}

    size_t argumentCount() const { return argumentCount_; }
    const ActionArgMetadata *arguments() const { return arguments_; }
    const ActionArgMetadata &argument(size_t index) const { return arguments_[index]; }
    // A RemoteAction may carry no schema at all, and still invokes normally.
    bool hasSchema() const { return arguments_ != nullptr && argumentCount_ != 0; }

    /// @brief Manager -> an action this device implements. The result survives
    /// this non-template boundary, so a correlated caller can return it while an
    /// ordinary MQTT /invoke simply drops it. A normalized payload can replace
    /// the String later without touching metadata or ownership.
    virtual ActionResult execute(const String &payload)
    {
        (void)payload;
        return {false, String()};
    }

protected:
    // Application -> remote owner.
    bool dispatchInvoke(const String &payload);

    const ActionArgMetadata *arguments_ = nullptr;
    size_t argumentCount_ = 0;

    friend class ResourcesManager;
};

/// @brief This device implements the action and runs it when invoked.
struct ManagedAction : public NetActionResource
{
    using Handler = ActionResult (*)(ManagedAction &action, const String &payload);

    ManagedAction(const String &resourceName)
        : NetActionResource(resourceName, String(), nullptr, 0, ResourceRole::MANAGED) {}

    /// @brief The argument count comes from the array, so only the constructor
    /// is generated per size and the class itself stays non-template.
    template <size_t N>
    ManagedAction(const String &resourceName, const ActionArgMetadata (&args)[N])
        : NetActionResource(resourceName, String(), args, N, ResourceRole::MANAGED) {}

    ManagedAction(const String &resourceName, const ActionArgMetadata *args, size_t argCount)
        : NetActionResource(resourceName, String(), args, argCount, ResourceRole::MANAGED) {}

    // This declaration is the authoritative contract the device publishes. The
    // handler still gets the canonical payload String and parses it itself.
    Handler onInvoke = nullptr;

    /// @brief Runs the local implementation and reports what it returned.
    ActionResult execute(const String &payload) override
    {
        if (onInvoke == nullptr)
            return {false, String()};
        return onInvoke(*this, payload);
    }
};

/// @brief Another device implements the action; invoke() sends the request.
///
/// It does not need to repeat the implementer's schema: the usual form declares
/// none and just invokes. A schema given here is what this caller knows and
/// expects, possibly a subset, never a claim to mirror the remote contract.
struct RemoteAction : public NetActionResource
{
    /// @brief Declared without a source yet; point it at one with setSource().
    RemoteAction()
        : NetActionResource(String(), String(), nullptr, 0, ResourceRole::REMOTE) {}

    RemoteAction(const String &resourceName, const NetDeviceIdentity &owner)
        : NetActionResource(resourceName, owner.deviceName, nullptr, 0, ResourceRole::REMOTE) {}

    template <size_t N>
    RemoteAction(const String &resourceName, const NetDeviceIdentity &owner,
                 const ActionArgMetadata (&args)[N])
        : NetActionResource(resourceName, owner.deviceName, args, N, ResourceRole::REMOTE) {}

    RemoteAction(const String &resourceName, const NetDeviceIdentity &owner,
                 const ActionArgMetadata *args, size_t argCount)
        : NetActionResource(resourceName, owner.deviceName, args, argCount, ResourceRole::REMOTE) {}

    /// @brief An expected schema with the source supplied later by setSource().
    RemoteAction(const String &resourceName, const ActionArgMetadata *args, size_t argCount)
        : NetActionResource(resourceName, String(), args, argCount, ResourceRole::REMOTE) {}

    /// @brief Points this at a different remote action. Stays REMOTE whatever
    /// device is named. The argument schema is a property of this declaration,
    /// so it is left alone.
    void setSource(const String &deviceName, const String &resourceName)
    {
        this->setRemoteSource(deviceName, resourceName);
    }

    /// @brief Application intent. True means the invocation was accepted for
    /// transport, not that the remote action ran or succeeded; use the
    /// controlled console or MQTTP path when the result matters. Fails when
    /// unbound, because nothing was sent.
    bool invoke(const String &payload = String()) { return dispatchInvoke(payload); }
};
