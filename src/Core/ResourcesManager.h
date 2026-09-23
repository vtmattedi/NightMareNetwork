#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_RESOURCES
#include "NetResources.h"
#include <ArduinoJson.h>

// Implement this at the transport boundary. A successful return means that the
// message was accepted for publishing; it does not acknowledge execution by another device.
class ResourcePublisher
{
public:
    virtual ~ResourcePublisher() = default;
    virtual bool publish(const String &topic, const String &payload, bool retained) = 0;
};

class ResourceSubscriber
{
public:
    virtual ~ResourceSubscriber() = default;
    virtual bool subscribe(const String &topicFilter) = 0;
    virtual bool unsubscribe(const String &topicFilter) = 0;
};
enum class InternalCommands
{
    NONE,
    LIST,
    MANIFEST,
    DROP,
    RAW,
};

InternalCommands parseInternalCommand(const String &command);

struct ParsedCommand
{
    bool internalSyntax = false;
    String internalAction;
    String target;
    String verb;
    String payload;
    InternalCommands internalCommand = InternalCommands::NONE;
};

ParsedCommand parseCommand(const String &expression);

/// @brief Registration, routing, manifests, subscriptions and reconnect
/// behaviour for declared resources. Deliberately non-template: it only ever
/// sees NetResource, NetValueResource, NetActionResource and String. Everything
/// type-specific lives in NetValue<T>, NetCodec<T> and ManagedState<T>.
///
/// It owns participation and routing, not identity: every topic comes from the
/// resource layer's resolveResourceTopic(), and a managed resource's owner is
/// always the current DeviceIdentity.
class ResourcesManager
{
public:
    static constexpr int MaxResources = 100;

    // Framework setup: publishing is injected by the MQTT facade.
    void setPublisher(ResourcePublisher *publisher);
    void setSubscriber(ResourceSubscriber *subscriber);

    // Project API: resources remain owned by the project and must outlive their
    // binding. A Remote resource may be bound before it has a source; it is
    // registered now and subscribed when setSource() supplies one.
    bool bindResource(NetResource *resource);
    void unbindResource(NetResource *resource);

    // Call after transport reconnection to republish the retained manifest and
    // every managed value that has authoritative state. Binding one also announces it.
    bool announceAll();
    // Rebuild exact subscriptions after a transport reconnects.
    void subscribeAll();
    bool needsSubscription(const String &topicFilter) const;

    /// @brief True when the message was resource traffic this Manager consumed,
    /// whether or not the operation succeeded. A rejected write, a failed
    /// action or an undecodable state still returns true, so it cannot leak
    /// into the application's generic MQTT callback. Only traffic that is not
    /// for a known resource returns false.
    bool handleIngressMessage(const String &topic, const String &message);

    // Called for valid manifests from other devices, including retained deletion
    // (an empty payload).
    using ManifestHandler = void (*)(const String &deviceName, const String &manifest);

    /// @brief Install a handler for every device's manifest, and subscribe to
    /// them. Setting one is the request: a handler wants the whole network, not
    /// only the devices this one happens to bind resources from, so the manager
    /// takes "+/resources" on its behalf and gives it back when the handler is
    /// cleared. Independent of NM_ENABLE_REMOTE_RESOURCE_VERIFICATION, which
    /// governs only the manager's own checking of its own Remote resources.
    ///
    /// The handler is stored before the subscription is asked for, deliberately.
    /// A subscribe is queued, not established, when the call returns, and a
    /// retained manifest can be delivered the instant the broker processes it --
    /// so a handler installed afterwards would miss exactly the replay it was
    /// set up to receive.
    void setManifestHandler(ManifestHandler handler);

    // Called for the compact MessagePack manifest of every device, once one of
    // these is installed. Separate from ManifestHandler on purpose: the two
    // topics carry the same document in different encodings, and a consumer
    // wants one of them, not both.
    using EncodedManifestHandler = void (*)(const String &deviceName, const String &encoded);

    /// @brief Install a handler for every device's compact manifest, and
    /// subscribe to them. Nothing subscribes to or decodes the MessagePack form
    /// unless one is set -- the library's own verification still reads the JSON
    /// manifest -- so this is what turns the compact form on for a reader.
    ///
    /// The payload handed over is the raw MessagePack, already checked to be
    /// well formed and to carry an encoding version this build understands. A
    /// manifest in a version it does not is dropped with a warning rather than
    /// guessed at; the JSON manifest is always published beside it, so a reader
    /// that cares can fall back to that.
    ///
    /// Stored before subscribing, for the same reason as setManifestHandler().
    void setEncodedManifestHandler(EncodedManifestHandler handler);

    /// @brief Expand a compact manifest into the same named-key document the
    /// JSON form has, so a consumer can read either through one code path and
    /// never hardcode a position. False when the payload is not well formed or
    /// its encoding version is not ManifestEncodingVersion.
    ///
    /// Optional: the handler receives the raw payload, and a consumer happy to
    /// read positions directly can skip this and keep the compact document.
    static bool decodeManifest(const String &encoded, JsonDocument &into);

    /// @brief Runs a locally implemented action and hands back what it returned.
    /// Raw MQTT ingress uses this and drops the result; a correlated caller
    /// (controlled console / MQTTP) uses the same path and keeps it, so action
    /// execution is never implemented twice.
    ActionResult executeAction(NetActionResource &action, const String &canonicalPayload);

    /// @brief Executes the resource-command expression after a leading `>`.
    /// `list`, `manifest`, `drop <name|owner/name>` and `raw <topic> <payload>` are
    /// manager operations (no space after `>`). A leading space selects a
    /// resource by unique short name or exact owner/name; a bare address
    /// performs its default operation and an optional verb selects get, set or
    /// invoke explicitly.
    ActionResult executeCommand(const String &expression);

    /// @brief Removes the retained resource footprint of a previous identity:
    /// the manifest and the state of every currently declared managed value.
    /// Only values declared now can be found, so one declared under the old name
    /// and since removed from the firmware is not reached. True only when every
    /// deletion was accepted for publishing, so a failure can be retried.
    bool withdrawIdentity(const String &oldDeviceName);

private:
    friend struct NetResource;
    friend struct NetValueResource;
    friend struct NetActionResource;

    // Resource methods delegate here. A managed write keeps local truth even if
    // publication fails; a remote request succeeds only when it was transported.
    bool setValue(NetValueResource &resource, const String &encoded);
    bool invoke(NetActionResource &resource, const String &payload);

    /// @brief A bound Remote resource was pointed at a different source. The old
    /// owner and name arrive explicitly because the resource has already been
    /// retargeted and its previous topics can no longer be reconstructed.
    void notifySourceChanged(NetResource &resource, const NetDeviceIdentity &oldOwner,
                             const String &oldName);

    static bool validSegment(const String &segment);
    static bool validActionSchema(const NetActionResource &action);
    // Every part of a Remote source is set; it may still be invalid.
    static bool sourceConfigured(const NetResource &resource);
    static bool hasResolvedSource(const NetResource &resource);
    NetResource *findResource(const String &deviceName, const String &name) const;
    NetResource *findResourceByName(const String &name, bool &ambiguous) const;
    NetResource *findCommandResource(const String &address, bool &ambiguous) const;
    bool addressTakenByOther(const String &deviceName, const String &name,
                             const NetResource *self) const;

    // The one ingress topic a resource needs, or empty when it needs none
    // (ManagedSensor, RemoteAction, or a Remote resource with no source yet).
    String ingressTopicFor(const NetResource &resource) const;
    String ingressTopicFor(const NetResource &resource, const String &deviceName,
                           const String &resourceName) const;
    // Manifests are subscribed once per remote device, not once per resource.
    bool remoteOwnerInUse(const String &deviceName, const NetResource *exclude) const;

    bool publishManifest();
    // One builder per encoding rather than one with branches: the JSON form is
    // frozen and the compact form is free to change, and keeping them apart is
    // what stops a change to the second quietly altering the first.
    void buildNamedManifest(JsonDocument &doc) const;
    void buildPositionalManifest(JsonDocument &doc) const;
    void applyEncodedManifest(const String &deviceName, const String &message);
    /// @brief Build this device's manifest in one encoding. The document is the
    /// same either way; MSGPACK writes kind, access and type as their numeric
    /// values rather than their names, which is most of what it saves.
    bool serializeManifest(String &payload, ManifestFormat format) const;
    /// @brief Whether a manifest subscription is wanted for remote owners.
    /// False when verification is compiled out: a handler subscribes to every
    /// device instead, which is a different question.
    static bool verifiesRemoteManifests();
    bool publishState(const NetValueResource &resource);
    ActionResult listResources() const;
    void applyOtherDeviceManifest(const String &deviceName, const String &message);
    void subscribeResource(const NetResource &resource, bool includeManifest);
    void unsubscribeResource(const NetResource &resource, bool removeManifest);

    // Consumers for recognised traffic. MQTT ingress consumes the message
    // either way; correlated command callers also use the returned outcome.
    bool applyRemoteState(NetValueResource &value, const String &message);
    bool applyManagedWrite(NetValueResource &value, const String &message);

    NetResource *resources_[MaxResources] = {};
    int resourceCount_ = 0;
    ResourcePublisher *publisher_ = nullptr;   // Non-owning.
    ResourceSubscriber *subscriber_ = nullptr; // Non-owning.
    ManifestHandler manifestHandler_ = nullptr;
    EncodedManifestHandler encodedManifestHandler_ = nullptr;
};

extern ResourcesManager gResourcesManager;
#endif // NM_ENABLE_RESOURCES
