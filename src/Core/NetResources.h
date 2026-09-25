#pragma once

#include <Arduino.h>
#include <NightMare/Features.h>
#include "NetCodec.h"

class ResourcesManager;
template <typename T>
class NetValue;

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

class NetResource
{
public:
    /// @brief Stable local registry/configuration identity.
    const String &name() const { return name_; }
    /// @brief Remote source owner (or this device for a Managed resource).
    const String &owner() const;
    /// @brief Remote source Resource segment; empty until configured.
    const String &sourceResource() const { return sourceResourceName_; }
    NetResourceType kind() const { return kind_; }
    bool isBound() const { return resourceManager_ != nullptr; }
    bool isRemote() const { return role_ == ResourceRole::REMOTE; }

protected:
    // A MANAGED resource leaves ownerDevice empty for good: its owner is always
    // the current device identity, read when a topic is resolved (see
    // resolveResourceOwner). Nothing is copied at construction or bind time, so
    // a global declaration never has to know the local name. A REMOTE one may
    // also start without a source and be pointed at one later with setSource().
    NetResource(const String &resourceName, const String &identity,
                const NetResourceType resourceType, const ResourceRole resourceRole)
        : kind_(resourceType),
          name_(resourceName),
          ownerDevice_(identity),
          role_(resourceRole),
          sourceResourceName_(resourceRole == ResourceRole::REMOTE && identity.length() != 0
                                  ? resourceName
                                  : String()) {};
    virtual ~NetResource() = default;
    /// @brief Points a REMOTE resource at a different source. Updates and
    /// persists the target only; the local name and role never change.
    bool setRemoteSource(const String &deviceName, const String &resourceName);
    bool clearRemoteSource();

    /// @brief Drops whatever was learned from the previous source.
    virtual void resetRemoteState() {}

private:
    const NetResourceType kind_;
    const String name_;
    NetDeviceIdentity ownerDevice_;
    const ResourceRole role_;
    String sourceResourceName_;
    ResourcesManager *resourceManager_ = nullptr; // Non-owning; set by bindResource().

    bool isOwned() const { return role_ == ResourceRole::MANAGED; }

    friend class ResourcesManager;
    friend class NetValueResource;
    friend class NetActionResource;
    friend const String &resolveResourceOwner(const NetResource &resource);
};

/* Canonical addressing. Topics belong to the resource layer, so a resource can
 * name itself without a ResourcesManager, and the Manager builds nothing of its
 * own. These only assemble strings: callers validate the segments first.
 *
 *   <device>/manifest                   manifest, retained
 *   <device>/manifest/msgpack           compact manifest, retained
 *   <device>/manifest/consume           remote dependencies, retained
 *   <device>/manifest/consume/msgpack   compact remote dependencies, retained
 *   <device>/resource/<name>/state      value state, retained
 *   <device>/resource/<name>/set        write request, transient
 *   <device>/resource/<name>/invoke     action request, transient
 */
enum class ResourceTopicOperation : uint8_t
{
    STATE,
    SET,
    INVOKE
};

/// @brief How a manifest is encoded on the wire. The same document either way:
/// this chooses the encoding, never the content.
///
///   JSON     <device>/manifest          readable, self-describing, large
///   MSGPACK  <device>/manifest/msgpack  compact: positions instead of keys,
///                                       enums as their value, not their name
///
/// Both live under <device>/manifest, which is a sibling of <device>/resource
/// and not inside it. What a device declares and what its resources currently
/// read are two different things, and keeping them in separate subtrees means a
/// reader can subscribe to one without the other -- `+/manifest` for discovery,
/// `+/resource/+/state` for values -- instead of filtering the manifest back
/// out of a resource wildcard.
///
/// Both are published and both are retained, so a reader picks whichever it can
/// decode and nothing has to negotiate. A manifest is the largest routine
/// document on the network and the one that arrives while a connection is at
/// its most expensive, which is what the compact form is for.
enum class ManifestFormat : uint8_t
{
    JSON,
    MSGPACK
};

/* ---------------------------------------------------------------------------
 * The MessagePack manifest layout.
 *
 * This is the normative description; nothing else should restate it.
 *
 *   manifest := [ encodingVersion:uint, manifestVersion:uint, resources:array ]
 *
 *   resource := [ 0, name:str, access:uint, type:uint, dependsOn:str? ]  // VALUE
 *             | [ 1, name:str, arguments:array ]                        // ACTION
 *
 *   argument := [ name:str, type:uint, required:bool ]
 *
 * `kind` leads each resource so a reader knows the shape before reading the
 * rest. The numbers are NetResourceType, AccessPolicy and NetValueType.
 *
 * `dependsOn` is the LOCAL name of the value this one mirrors, present only
 * when one was declared, so a value without one encodes exactly as it did
 * before the field existed. It is a single name, never an array and never
 * `<device>/<resource>`: see NetValueResource::setDependency().
 *
 * Positions rather than keys because MessagePack has no string table: it writes
 * every key in full, every time, and on a real 20-resource manifest the repeated
 * words "name", "kind", "access" and "type" were 61% of the payload. Integer
 * keys are not an option here -- ArduinoJson's object keys are JsonString on
 * both encode and decode -- and positions cost nothing at all. Measured against
 * that manifest: 1753 bytes as JSON, 955 keyed, 375 positional.
 *
 * The price is that ORDER IS THE CONTRACT, which is what the encoding version
 * exists to govern:
 *
 *  1. Element 0 of the top-level array is the encoding version, frozen forever.
 *     Every future version keeps it at position 0, so a reader can always learn
 *     what it is holding before trying to interpret the rest.
 *
 *  2. Append only. A new field goes on the END of its array. Readers ignore
 *     trailing elements they do not recognise, so adding one does NOT bump the
 *     encoding version.
 *
 *  3. Never reorder a position, never repurpose one, never remove one. Each of
 *     those breaks every existing reader and DOES bump the encoding version.
 *
 *  4. Enum values are append-only and never renumbered. A value a reader does
 *     not know means "unknown", not "incompatible": it neither bumps the
 *     encoding version nor counts as a mismatch.
 *
 *  5. An unknown encoding version is not an error. The reader ignores the
 *     compact manifest and uses the JSON one, which is always published beside
 *     it. That is what makes rule 3 survivable -- a bump degrades old readers to
 *     JSON instead of breaking them.
 * ------------------------------------------------------------------------- */
constexpr uint8_t ManifestEncodingVersion = 1;
constexpr uint8_t ConsumeManifestEncodingVersion = 1;
/// Version 2 appends `remotes` (position 3 of the compact form): every Remote
/// Resource this device declares, bound or not, so a controller can find and
/// configure them with SOURCE. `consumes` is unchanged and still lists only
/// dependency edges. Version 1 readers ignore the extra element.
///
///   remote := [ 0, localName:str, bound:bool, device:str, resource:str, access:uint, type:uint ]
///           | [ 1, localName:str, bound:bool, device:str, resource:str, arguments:array ]
///
/// device and resource are empty strings while unbound.
constexpr uint8_t ConsumeManifestVersion = 2;

/// Positions within the top-level array. Position 0 is fixed for all time; see
/// rule 1 above.
enum class ManifestSlot : uint8_t
{
    ENCODING_VERSION = 0,
    MANIFEST_VERSION = 1,
    RESOURCES = 2
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

/// @brief `<device>/manifest`: the JSON manifest, and the root of the subtree
/// the compact one hangs below.
String resolveResourceManifestTopic(const String &deviceName);

/// @brief The manifest topic for one encoding. JSON is the bare
/// `<device>/manifest`; MSGPACK adds `/msgpack` below it. This encoding-named
/// path leaves room for siblings such as `/manifest/cbor`.
String resolveResourceManifestTopic(const String &deviceName, ManifestFormat format);

/// @brief Retained dependencies derived from bound, resolved Remote Resources.
String resolveResourceConsumeManifestTopic(const String &deviceName,
                                           ManifestFormat format = ManifestFormat::JSON);

/// @brief `<device>/resource`: the root every resource hangs below. Separate
/// from the manifest topic, deliberately -- see ManifestFormat.
String resolveResourceRootTopic(const String &deviceName);

enum class NetSyncStrategy : uint8_t
{
    OPTIMISTIC, // A local write shadows owner state for a short window.
    STRICT,     // Always report the owner's state, even mid-change.
};

/// @brief Manager-facing half of a value resource: everything that does not
/// depend on the value's C++ type, including the optimistic/authoritative state
/// machine. The typed container is NetValue<T>.
class NetValueResource : public NetResource
{
public:
    NetValueType type() const { return valueType_; }

    /// @brief The value this one mirrors, or nullptr. See setDependency().
    const NetValueResource *dependency() const { return dependency_; }

protected:
    /// @brief Declares that this value IS `source`: the same logical value,
    /// with `source` authoritative. Every time `source` receives an
    /// authoritative update the Manager copies it here and publishes this
    /// resource's state, with no application code in between.
    ///
    /// This is mirroring, not derivation. `dependsOn` asserts semantic
    /// identity, and only the application can know whether two resources
    /// really mean the same thing: matching wire types is all that can be
    /// checked here, and `occupied.dependsOn(doorOpen)` passes that check while
    /// being nonsense. Declare it only between resources that represent the
    /// same fact.
    ///
    /// The edge is published as `source`'s LOCAL name, never
    /// `<device>/<resource>`. That is what keeps it stable: a Remote source
    /// keeps its local identity while its own source is reconfigured, so
    /// retargeting `door` from Mycroft to Moriarty leaves everything that
    /// mirrors `door` untouched.
    ///
    /// When `source` loses its value -- its retained state is withdrawn, it is
    /// retargeted, or it is unbound -- this resource goes stale and its own
    /// retained state is tombstoned, because a mirror cannot keep asserting a
    /// value nothing stands behind.
    ///
    /// A value has at most one dependency and the last call wins. `source`
    /// must outlive this resource, which it does when both are the usual
    /// long-lived declarations.
    bool setDependency(const NetValueResource &source);


    NetValueResource(const String &resourceName, const String &resourceOwner,
                     AccessPolicy resourceAccess, NetValueType resourceValueType,
                     ResourceRole resourceRole)
        : NetResource(resourceName, resourceOwner, NetResourceType::VALUE, resourceRole),
          access_(resourceAccess),
          valueType_(resourceValueType) {}

    bool hasValueImpl() const { return hasAuthoritativeValue_ || optimisticActive(); }
    bool isStaleImpl() const { return freshness_ == ResourceFreshness::STALE; }
    void useOptimisticSync() { syncStrategy_ = NetSyncStrategy::OPTIMISTIC; }

    // Type-erasure boundary. These are the only value operations the transport
    // layer needs, and all three speak the encoded wire format.
private:
    AccessPolicy access_ = AccessPolicy::READ;
    NetValueType valueType_ = NetValueType::STRING;
    // Non-owning, like resourceManager_: declarations outlive each other.
    const NetValueResource *dependency_ = nullptr;
    NetSyncStrategy syncStrategy_ = NetSyncStrategy::STRICT;
    uint32_t optimisticWindowMs_ = 5000;
    ResourceFreshness freshness_ = ResourceFreshness::UNKNOWN;

    virtual String encodedValue() const = 0;
    virtual String encodedCurrentValue() const = 0;
    // A local caller requests a value using its wire representation. Remote
    // values retain their normal optimistic-write behaviour through this path.
    virtual bool requestEncodedValue(const String &encoded) = 0;
    // Ingress of owner state: this value is now the truth.
    virtual bool applyEncodedOwnerValue(const String &encoded) = 0;
    // Ingress of a /set request aimed at a value this device owns.
    virtual bool applyEncodedWrite(const String &encoded) = 0;

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
    template <typename T>
    friend class NetValue;
};

/// @brief The typed value container. Holds the owner's truth and the last
/// locally requested value, and converts at the wire boundary through
/// NetCodec<T>. Deliberately thin: the framework logic lives in the base.
template <typename T>
class NetValue : public NetValueResource
{
public:
    using UpdateHandler = void (*)(NetValue<T> &resource, const T &value);

    /// @brief Fires after the effective value actually changes.
    UpdateHandler onUpdate = nullptr;

protected:
    /// @brief A value owned by this device.
    NetValue(const String &resourceName, AccessPolicy resourceAccess = AccessPolicy::READ)
        : NetValueResource(resourceName, String(), resourceAccess, NetCodec<T>::Type,
                           ResourceRole::MANAGED) {}

    /// @brief A value owned by another device.
    NetValue(const String &resourceName, const NetDeviceIdentity &owner,
             AccessPolicy resourceAccess = AccessPolicy::READ)
        : NetValueResource(resourceName, owner.deviceName, resourceAccess, NetCodec<T>::Type,
                           ResourceRole::REMOTE) {}

    /// @brief The value the application should act on: the optimistic value
    /// while its window is open, the owner's value otherwise.
    const T &getValueImpl() const
    {
        return optimisticActive() ? optimisticValue_ : authoritativeValue_;
    }

    bool setManagedValue(const T &value, bool applyWritePolicy)
    {
        const String encoded = NetCodec<T>::encode(value);
        if (encoded.length() == 0 || encoded.length() > NetResourceMaxPayloadLength)
            return false;
        if (applyWritePolicy && !acceptManagedWrite(value))
            return false;

        const T previous = getValueImpl();
        commitOwnerValue(value);
        notifyIfEffectiveChanged(previous);
        return dispatchLocalWrite(encoded);
    }

    bool setRemoteValue(const T &value)
    {
        const T previous = getValueImpl();
        if (!dispatchLocalWrite(NetCodec<T>::encode(value)))
            return false;
        optimisticValue_ = value;
        noteLocalWrite();
        notifyIfEffectiveChanged(previous);
        return true;
    }

    bool hasValueImpl() const { return NetValueResource::hasValueImpl(); }
    bool isStaleImpl() const { return NetValueResource::isStaleImpl(); }

private:
    String encodedValue() const override { return NetCodec<T>::encode(authoritativeValue_); }
    String encodedCurrentValue() const override { return NetCodec<T>::encode(getValueImpl()); }

    bool requestEncodedValue(const String &encoded) override
    {
        T requested = T();
        if (!NetCodec<T>::decode(encoded, requested))
            return false;
        return setRemoteValue(requested);
    }

    bool applyEncodedOwnerValue(const String &encoded) override
    {
        T decoded = T();
        if (!NetCodec<T>::decode(encoded, decoded))
            return false;
        const T previous = getValueImpl();
        commitOwnerValue(decoded);
        notifyIfEffectiveChanged(previous);
        return true;
    }

    bool applyEncodedWrite(const String &encoded) override
    {
        T requested = T();
        if (!NetCodec<T>::decode(encoded, requested))
            return false;
        if (!acceptManagedWrite(requested))
            return false;
        const T previous = getValueImpl();
        commitOwnerValue(requested);
        notifyIfEffectiveChanged(previous);
        return true;
    }

    virtual bool acceptManagedWrite(const T &requested)
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

    void commitOwnerValue(const T &value)
    {
        authoritativeValue_ = value;
        hasAuthoritativeValue_ = true;
        noteOwnerUpdate();
    }

    void notifyIfEffectiveChanged(const T &previous)
    {
        if (onUpdate != nullptr && !(getValueImpl() == previous))
            onUpdate(*this, getValueImpl());
    }

    T authoritativeValue_ = T();
    T optimisticValue_ = T();
};

/* These four leaf types are the application API. NetValue<T> is their
 * non-instantiable implementation base, so invalid operations are absent from
 * each leaf rather than present and rejected at runtime. */

/// @brief Owned by this device, observe-only for everyone else.
template <typename T>
class ManagedSensor : public NetValue<T>
{
public:
    ManagedSensor(const String &resourceName)
        : NetValue<T>(resourceName, AccessPolicy::READ) {}

    const T &getValue() const { return this->getValueImpl(); }
    bool setValue(const T &value) { return this->setManagedValue(value, false); }

    /* dependsOn() is here and nowhere else. A Remote value already mirrors its
     * source, an action has no value to mirror, and ManagedState is waiting on
     * write-through semantics, so in each case the operation is absent rather
     * than present and rejected -- the same reason setValue() is absent from
     * RemoteSensor. */

    /// @brief Mirrors `source`, which becomes authoritative for this value.
    /// One dependency, last call wins; see NetValueResource::setDependency().
    ManagedSensor<T> &dependsOn(NetValueResource &source)
    {
        this->setDependency(source);
        return *this;
    }
};

/// @brief Owned by another device, observe-only. Reports owner state at all
/// times: nothing local can write it, so there is nothing to be optimistic about.
template <typename T>
class RemoteSensor : public NetValue<T>
{
public:
    /// @brief Stable local identity; point it at a source with setSource().
    explicit RemoteSensor(const String &localName)
        : NetValue<T>(localName, NetDeviceIdentity(String()), AccessPolicy::READ) {}

    RemoteSensor(const String &resourceName, const NetDeviceIdentity &owner)
        : NetValue<T>(resourceName, owner, AccessPolicy::READ)
    {}

    const T &getValue() const { return this->getValueImpl(); }
    bool hasValue() const { return this->hasValueImpl(); }
    bool isStale() const { return this->isStaleImpl(); }

    /// @brief Points this at a different remote resource. Stays REMOTE whatever
    /// device is named, and drops everything learned from the old source.
    bool setSource(const String &deviceName, const String &resourceName)
    {
        return this->setRemoteSource(deviceName, resourceName);
    }
    bool clearSource() { return this->clearRemoteSource(); }
};

/// @brief Owned by this device and writable by others. The handler receives the
/// already-decoded value: String conversion stops at the resource boundary.
template <typename T>
class ManagedState : public NetValue<T>
{
public:
    /// @brief Returns true to accept the request, which then becomes the
    /// authoritative value, or false to reject it and leave state untouched.
    using WriteRequestHandler = bool (*)(ManagedState<T> &state, const T &requested);

    ManagedState(const String &resourceName)
        : NetValue<T>(resourceName, AccessPolicy::READ_WRITE) {}

    WriteRequestHandler onWrite = nullptr;

    const T &getValue() const { return this->getValueImpl(); }
    bool setValue(const T &value) { return this->setManagedValue(value, true); }

    /* No dependsOn() here, deliberately. A mirror of another value is
     * read-only by construction, and this one accepts writes: a /set arriving
     * for it would have to travel to whoever is authoritative and come back as
     * an ordinary mirrored update, which is write-through and is not designed
     * yet. Until it is, ManagedSensor<T> is the only dependent, so there is no
     * writable resource quietly advertising a value it cannot change. */

private:
    bool acceptManagedWrite(const T &requested) override
    {
        return onWrite != nullptr && onWrite(*this, requested);
    }
};

/// @brief Owned by another device and writable from here. A local write shows
/// immediately and holds until the owner catches up or the window closes.
template <typename T>
class RemoteState : public NetValue<T>
{
public:
    /// @brief Stable local identity; point it at a source with setSource().
    explicit RemoteState(const String &localName)
        : NetValue<T>(localName, NetDeviceIdentity(String()), AccessPolicy::READ_WRITE)
    {
        this->useOptimisticSync();
    }

    RemoteState(const String &resourceName, const NetDeviceIdentity &owner)
        : NetValue<T>(resourceName, owner, AccessPolicy::READ_WRITE)
    {
        this->useOptimisticSync();
    }

    const T &getValue() const { return this->getValueImpl(); }
    bool setValue(const T &value) { return this->setRemoteValue(value); }
    bool hasValue() const { return this->hasValueImpl(); }
    bool isStale() const { return this->isStaleImpl(); }

    /// @brief Points this at a different remote resource. Stays REMOTE whatever
    /// device is named, and drops everything learned from the old source,
    /// including any optimistic window still open against it.
    bool setSource(const String &deviceName, const String &resourceName)
    {
        return this->setRemoteSource(deviceName, resourceName);
    }
    bool clearSource() { return this->clearRemoteSource(); }
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
class NetActionResource : public NetResource
{
public:
    size_t argumentCount() const { return argumentCount_; }
    const ActionArgMetadata *arguments() const { return arguments_; }
    const ActionArgMetadata &argument(size_t index) const { return arguments_[index]; }
    bool hasSchema() const { return arguments_ != nullptr && argumentCount_ != 0; }

protected:
    /// @param args Not copied. The schema has to outlive the action, so a static
    /// or global array is the expected source.
    NetActionResource(const String &resourceName, const String &resourceOwner,
                      const ActionArgMetadata *args, size_t argCount,
                      ResourceRole resourceRole)
        : NetResource(resourceName, resourceOwner, NetResourceType::ACTION, resourceRole),
          arguments_(args),
          argumentCount_(argCount) {}

private:
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

private:
    const ActionArgMetadata *arguments_ = nullptr;
    size_t argumentCount_ = 0;

    friend class ResourcesManager;
};

/// @brief This device implements the action and runs it when invoked.
class ManagedAction : public NetActionResource
{
public:
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

private:
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
class RemoteAction : public NetActionResource
{
public:
    /// @brief Stable local identity; point it at a source with setSource().
    explicit RemoteAction(const String &localName)
        : NetActionResource(localName, String(), nullptr, 0, ResourceRole::REMOTE) {}

    template <size_t N>
    RemoteAction(const String &localName, const ActionArgMetadata (&args)[N])
        : NetActionResource(localName, String(), args, N, ResourceRole::REMOTE) {}

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
    bool setSource(const String &deviceName, const String &resourceName)
    {
        return this->setRemoteSource(deviceName, resourceName);
    }
    bool clearSource() { return this->clearRemoteSource(); }

    /// @brief Application intent. True means the invocation was accepted for
    /// transport, not that the remote action ran or succeeded; use the
    /// controlled console or MQTTP path when the result matters. Fails when
    /// unbound, because nothing was sent.
    bool invoke(const String &payload = String()) { return dispatchInvoke(payload); }
};
