#include <NightMare/Features.h>
#if NM_ENABLE_RESOURCES
#include "ResourcesManager.h"
#include "DeviceIdentity.h"
#include "DocumentPayload.h"

#include <ArduinoJson.h>
#if NM_PLATFORM_ESP32
#include <esp_heap_caps.h>
#endif

namespace
{
    constexpr size_t MaxSegmentLength = 64;
    constexpr size_t MaxValueLength = NetResourceMaxPayloadLength;
    constexpr size_t MaxManifestLength = NetResourceMaxManifestLength;
    constexpr int ManifestVersion = 2;
    /// Every device's manifest, for a handler that wants the whole network.
    constexpr const char *AllManifestsFilter = "+/manifest";
    /// The same, in the compact encoding. Taken only when an encoded handler is set.
    constexpr const char *AllEncodedManifestsFilter = "+/manifest/msgpack";

/// @brief The encoding `>manifest` uses when given no argument. Written as a
/// bare word in NightMareConfig.h -- `#define NM_DEFAULT_MANIFEST_FORMAT mpack`
/// -- so the setting reads the way the command does.
#define NM_MANIFEST_FORMAT_STR2(x) #x
#define NM_MANIFEST_FORMAT_STR(x) NM_MANIFEST_FORMAT_STR2(x)

    /// @brief "json" or "mpack", case-insensitively. Empty selects the default.
    bool parseManifestFormat(const String &text, ManifestFormat &format)
    {
        String wanted = text;
        wanted.trim();
        if (wanted.length() == 0)
            wanted = NM_MANIFEST_FORMAT_STR(NM_DEFAULT_MANIFEST_FORMAT);
        wanted.toLowerCase();

        if (wanted == "json")
        {
            format = ManifestFormat::JSON;
            return true;
        }
        if (wanted == "mpack" || wanted == "msgpack")
        {
            format = ManifestFormat::MSGPACK;
            return true;
        }
        return false;
    }

    bool commandSpace(char c)
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
    }

    size_t skipCommandSpace(const String &text, size_t position)
    {
        while (position < text.length() && commandSpace(text[position]))
            ++position;
        return position;
    }

    String commandToken(const String &text, size_t &position)
    {
        position = skipCommandSpace(text, position);
        const size_t start = position;
        while (position < text.length() && !commandSpace(text[position]))
            ++position;
        return text.substring(start, position);
    }

    const char *kindName(NetResourceType kind)
    {
        return kind == NetResourceType::VALUE ? "value" : "action";
    }

    const char *accessName(AccessPolicy access)
    {
        return access == AccessPolicy::READ_WRITE ? "read_write" : "read";
    }

    const char *valueTypeName(NetValueType type)
    {
        switch (type)
        {
        case NetValueType::BOOLEAN:
            return "boolean";
        case NetValueType::INTEGER:
            return "integer";
        case NetValueType::FLOAT:
            return "float";
        case NetValueType::STRUCT:
            return "struct";
        case NetValueType::STRING:
        default:
            return "string";
        }
    }

#if NM_ENABLE_ACTION_PAYLOAD_ASSERTION
    bool argumentTypeMatches(NetValueType type, JsonVariantConst value)
    {
        switch (type)
        {
        case NetValueType::BOOLEAN:
            return value.is<bool>();
        case NetValueType::INTEGER:
            return value.is<long long>();
        case NetValueType::FLOAT:
            return value.is<double>() || value.is<long long>();
        case NetValueType::STRING:
            return value.is<const char *>();
        case NetValueType::STRUCT:
            return value.is<JsonObjectConst>() || value.is<JsonArrayConst>();
        }
        return false;
    }

    // Checks what the schema knows and nothing more. A tolerant reader: unknown
    // fields pass, so a newer caller can send fields an older implementation does
    // not declare yet, and an optional argument may be absent. An empty payload is
    // an object with no fields. JSON is the canonical machine payload; positional
    // human syntax belongs to the command layer and never arrives here.
    bool assertActionPayload(const NetActionResource &action, const String &payload, String &error)
    {
        JsonDocument doc;
        JsonObjectConst fields;
        if (payload.length() != 0)
        {
            if (deserializeJson(doc, payload) || !doc.is<JsonObject>())
            {
                error = "payload is not a JSON object";
                return false;
            }
            fields = doc.as<JsonObjectConst>();
        }
        for (size_t i = 0; i < action.argumentCount(); ++i)
        {
            const ActionArgMetadata &arg = action.argument(i);
            JsonVariantConst value = fields[arg.name];
            if (value.isNull())
            {
                if (!arg.required)
                    continue;
                error = String("missing argument '") + arg.name + "'";
                return false;
            }
            if (!argumentTypeMatches(arg.type, value))
            {
                error = String("argument '") + arg.name + "' is not " + valueTypeName(arg.type);
                return false;
            }
        }
        return true;
    }
#endif
}

InternalCommands parseInternalCommand(const String &command)
{
    String normalized = command;
    normalized.toUpperCase();
    if (normalized == "LIST")
        return InternalCommands::LIST;
    if (normalized == "MANIFEST")
        return InternalCommands::MANIFEST;
    if (normalized == "DROP")
        return InternalCommands::DROP;
    if (normalized == "RAW")
        return InternalCommands::RAW;
    return InternalCommands::NONE;
}

ParsedCommand parseCommand(const String &expression)
{
    ParsedCommand result;
    if (expression.length() == 0)
        return result;

    // The character immediately after `>` is part of the grammar: a manager
    // action starts immediately, while resource addressing starts with space.
    result.internalSyntax = !commandSpace(expression[0]);
    size_t position = 0;
    const String first = commandToken(expression, position);

    if (result.internalSyntax)
    {
        result.internalAction = first;
        result.internalCommand = parseInternalCommand(first);
        if (result.internalCommand == InternalCommands::RAW)
        {
            result.target = commandToken(expression, position);
            if (position < expression.length())
                result.payload = expression.substring(position + 1);
        }
        else if (position < expression.length())
        {
            // Keep manager arguments opaque so each manager action owns its
            // own argument grammar.
            result.payload = expression.substring(position + 1);
        }
        return result;
    }

    result.target = first;
    const size_t verbStart = skipCommandSpace(expression, position);
    if (verbStart >= expression.length())
        return result;
    position = verbStart;
    result.verb = commandToken(expression, position);
    if (position < expression.length())
        result.payload = expression.substring(position + 1);
    return result;
}

ResourcesManager gResourcesManager;

bool ResourcesManager::validSegment(const String &segment)
{
    if (segment.length() == 0 || segment.length() > MaxSegmentLength)
        return false;
    for (size_t i = 0; i < segment.length(); ++i)
    {
        const char c = segment[i];
        if (c == '/' || c == '+' || c == '#' || static_cast<uint8_t>(c) < 0x20)
            return false;
    }
    return true;
}

bool ResourcesManager::validActionSchema(const NetActionResource &action)
{
    for (size_t i = 0; i < action.argumentCount(); ++i)
    {
        const ActionArgMetadata &arg = action.argument(i);
        if (arg.name == nullptr || !validSegment(String(arg.name)))
            return false;
        for (size_t j = 0; j < i; ++j)
            if (strcmp(action.argument(j).name, arg.name) == 0)
                return false;
    }
    return true;
}

void ResourcesManager::setPublisher(ResourcePublisher *publisher)
{
    publisher_ = publisher;
    if (publisher_ != nullptr)
        announceAll();
}

void ResourcesManager::setSubscriber(ResourceSubscriber *subscriber)
{
    subscriber_ = subscriber;
}

bool ResourcesManager::sourceConfigured(const NetResource &resource)
{
    return resource.isOwned() ||
           (resource.ownerDevice_.deviceName.length() != 0 && resource.name_.length() != 0);
}

bool ResourcesManager::hasResolvedSource(const NetResource &resource)
{
    return DeviceIdentity::validDeviceName(resolveResourceOwner(resource)) &&
           validSegment(resource.name_);
}

bool ResourcesManager::remoteOwnerInUse(const String &deviceName, const NetResource *exclude) const
{
    for (int i = 0; i < resourceCount_; ++i)
    {
        const NetResource *resource = resources_[i];
        if (resource == exclude || resource->isOwned())
            continue;
        if (hasResolvedSource(*resource) && resource->ownerDevice_.deviceName == deviceName)
            return true;
    }
    return false;
}

String ResourcesManager::ingressTopicFor(const NetResource &resource, const String &deviceName,
                                         const String &resourceName) const
{
    if (!DeviceIdentity::validDeviceName(deviceName) || !validSegment(resourceName))
        return String();

    if (resource.kind_ == NetResourceType::VALUE)
    {
        // Remote values listen to their owner. Managed values only listen when
        // somebody else is allowed to write them.
        if (!resource.isOwned())
            return resolveResourceTopic(deviceName, resourceName, ResourceTopicOperation::STATE);
        const NetValueResource &value = static_cast<const NetValueResource &>(resource);
        if (value.access_ == AccessPolicy::READ_WRITE)
            return resolveResourceTopic(deviceName, resourceName, ResourceTopicOperation::SET);
        return String();
    }

    // Only the implementing device listens for invocations.
    if (resource.isOwned())
        return resolveResourceTopic(deviceName, resourceName, ResourceTopicOperation::INVOKE);
    return String();
}

String ResourcesManager::ingressTopicFor(const NetResource &resource) const
{
    return ingressTopicFor(resource, resolveResourceOwner(resource), resource.name_);
}

bool ResourcesManager::verifiesRemoteManifests()
{
    return NM_ENABLE_REMOTE_RESOURCE_VERIFICATION != 0;
}

void ResourcesManager::setManifestHandler(ManifestHandler handler)
{
    const bool had = manifestHandler_ != nullptr;
    // Stored first. See the note on the declaration: the subscription below is
    // only queued when it returns, and a retained manifest can arrive before
    // anyone would have installed a handler afterwards.
    manifestHandler_ = handler;
    if (subscriber_ == nullptr || had == (handler != nullptr))
        return;
    if (handler != nullptr)
        subscriber_->subscribe(AllManifestsFilter);
    else
        subscriber_->unsubscribe(AllManifestsFilter);
}

void ResourcesManager::setEncodedManifestHandler(EncodedManifestHandler handler)
{
    const bool had = encodedManifestHandler_ != nullptr;
    // Stored before subscribing, as above.
    encodedManifestHandler_ = handler;
    if (subscriber_ == nullptr || had == (handler != nullptr))
        return;
    if (handler != nullptr)
        subscriber_->subscribe(AllEncodedManifestsFilter);
    else
        subscriber_->unsubscribe(AllEncodedManifestsFilter);
}

bool ResourcesManager::decodeManifest(const String &encoded, JsonDocument &into)
{
    JsonDocument packed;
    if (deserializeMsgPack(packed, encoded.c_str(), encoded.length()) || !packed.is<JsonArray>())
        return false;

    JsonArrayConst root = packed.as<JsonArrayConst>();
    // Position 0 is the encoding version and always will be; anything else is a
    // dialect this build does not speak. Refusing beats guessing -- the JSON
    // manifest is published beside it, so the caller has somewhere to go.
    if (root.size() < 3 || root[0].as<uint8_t>() != ManifestEncodingVersion)
        return false;

    into.clear();
    into["version"] = root[1].as<int>();
    JsonArray items = into["resources"].to<JsonArray>();
    for (JsonVariantConst element : root[2].as<JsonArrayConst>())
    {
        JsonArrayConst entry = element.as<JsonArrayConst>();
        if (entry.size() < 2)
            continue;
        const uint8_t kind = entry[0].as<uint8_t>();
        JsonObject item = items.add<JsonObject>();
        item["name"] = entry[1].as<const char *>();
        item["kind"] = kindName(static_cast<NetResourceType>(kind));

        if (kind == static_cast<uint8_t>(NetResourceType::VALUE))
        {
            if (entry.size() < 4)
                continue;
            item["access"] = accessName(static_cast<AccessPolicy>(entry[2].as<uint8_t>()));
            item["type"] = valueTypeName(static_cast<NetValueType>(entry[3].as<uint8_t>()));
            continue;
        }

        JsonArray args = item["arguments"].to<JsonArray>();
        if (entry.size() < 3)
            continue;
        for (JsonVariantConst argumentElement : entry[2].as<JsonArrayConst>())
        {
            JsonArrayConst argument = argumentElement.as<JsonArrayConst>();
            if (argument.size() < 3)
                continue;
            JsonObject out = args.add<JsonObject>();
            out["name"] = argument[0].as<const char *>();
            out["type"] = valueTypeName(static_cast<NetValueType>(argument[1].as<uint8_t>()));
            out["required"] = argument[2].as<bool>();
        }
    }
    return true;
}

bool ResourcesManager::decodeConsumeManifest(const String &encoded, JsonDocument &into)
{
    JsonDocument packed;
    if (deserializeMsgPack(packed, encoded.c_str(), encoded.length()) ||
        !packed.is<JsonArray>())
        return false;

    JsonArrayConst root = packed.as<JsonArrayConst>();
    if (root.size() < 3 ||
        root[0].as<uint8_t>() != ConsumeManifestEncodingVersion ||
        !root[2].is<JsonArrayConst>())
        return false;

    into.clear();
    into["version"] = root[1].as<int>();
    JsonArray consumes = into["consumes"].to<JsonArray>();
    for (JsonVariantConst element : root[2].as<JsonArrayConst>())
    {
        JsonArrayConst entry = element.as<JsonArrayConst>();
        if (entry.size() < 3)
            return false;
        const uint8_t kind = entry[0].as<uint8_t>();
        JsonObject item = consumes.add<JsonObject>();
        item["device"] = entry[1].as<const char *>();
        item["resource"] = entry[2].as<const char *>();
        if (kind == static_cast<uint8_t>(NetResourceType::VALUE))
        {
            if (entry.size() < 5)
                return false;
            item["kind"] = "value";
            item["access"] = accessName(static_cast<AccessPolicy>(entry[3].as<uint8_t>()));
            item["type"] = valueTypeName(static_cast<NetValueType>(entry[4].as<uint8_t>()));
        }
        else if (kind == static_cast<uint8_t>(NetResourceType::ACTION))
        {
            if (entry.size() < 4 || !entry[3].is<JsonArrayConst>())
                return false;
            item["kind"] = "action";
            JsonArray args = item["arguments"].to<JsonArray>();
            for (JsonVariantConst argumentElement : entry[3].as<JsonArrayConst>())
            {
                JsonArrayConst argument = argumentElement.as<JsonArrayConst>();
                if (argument.size() < 3)
                    return false;
                JsonObject out = args.add<JsonObject>();
                out["name"] = argument[0].as<const char *>();
                out["type"] = valueTypeName(
                    static_cast<NetValueType>(argument[1].as<uint8_t>()));
                out["required"] = argument[2].as<bool>();
            }
        }
        else
            return false;
    }
    return true;
}

void ResourcesManager::subscribeResource(const NetResource &resource, bool includeManifest)
{
    if (subscriber_ == nullptr)
        return;
    // A remote owner's manifest is only worth a subscription if something is
    // going to read it. With verification compiled out nothing here does, and a
    // handler -- which wants every device, not just the bound ones -- has its
    // own subscription. The compact encoding, because that is the one the
    // manager reads; a JSON reader is a handler and subscribes for itself.
    if (verifiesRemoteManifests() && includeManifest && !resource.isOwned() &&
        hasResolvedSource(resource))
        subscriber_->subscribe(resolveResourceManifestTopic(resource.ownerDevice_.deviceName,
                                                            ManifestFormat::MSGPACK));
    const String ingress = ingressTopicFor(resource);
    if (ingress.length() != 0)
        subscriber_->subscribe(ingress);
}

void ResourcesManager::unsubscribeResource(const NetResource &resource, bool removeManifest)
{
    if (subscriber_ == nullptr)
        return;
    const String ingress = ingressTopicFor(resource);
    if (ingress.length() != 0)
        subscriber_->unsubscribe(ingress);
    if (verifiesRemoteManifests() && removeManifest && !resource.isOwned() &&
        hasResolvedSource(resource))
        subscriber_->unsubscribe(resolveResourceManifestTopic(resource.ownerDevice_.deviceName,
                                                              ManifestFormat::MSGPACK));
}

void ResourcesManager::subscribeAll()
{
    // Reinstalled on every reconnect alongside the per-resource ones: the
    // handler outlives the connection that was carrying manifests to it.
    if (manifestHandler_ != nullptr && subscriber_ != nullptr)
        subscriber_->subscribe(AllManifestsFilter);
    if (encodedManifestHandler_ != nullptr && subscriber_ != nullptr)
        subscriber_->subscribe(AllEncodedManifestsFilter);

    for (int i = 0; i < resourceCount_; ++i)
    {
        const NetResource &resource = *resources_[i];
        // One manifest subscription per remote device: claim it for the first
        // resource that names that owner.
        bool includeManifest = !resource.isOwned() && hasResolvedSource(resource);
        for (int j = 0; includeManifest && j < i; ++j)
            if (!resources_[j]->isOwned() && hasResolvedSource(*resources_[j]) &&
                resources_[j]->ownerDevice_.deviceName == resource.ownerDevice_.deviceName)
                includeManifest = false;
        subscribeResource(resource, includeManifest);
    }
}

bool ResourcesManager::needsSubscription(const String &topicFilter) const
{
    if (topicFilter.length() == 0)
        return false;
    // Claimed so a project asking to drop this filter cannot take the handler's
    // manifests away with it.
    if (manifestHandler_ != nullptr && topicFilter == AllManifestsFilter)
        return true;
    if (encodedManifestHandler_ != nullptr && topicFilter == AllEncodedManifestsFilter)
        return true;
    for (int i = 0; i < resourceCount_; ++i)
    {
        const NetResource &resource = *resources_[i];
        if (ingressTopicFor(resource) == topicFilter)
            return true;
        if (verifiesRemoteManifests() && !resource.isOwned() && hasResolvedSource(resource) &&
            resolveResourceManifestTopic(resource.ownerDevice_.deviceName,
                                         ManifestFormat::MSGPACK) == topicFilter)
            return true;
    }
    return false;
}

NetResource *ResourcesManager::findResource(const String &deviceName, const String &name) const
{
    for (int i = 0; i < resourceCount_; ++i)
    {
        NetResource *resource = resources_[i];
        if (resolveResourceOwner(*resource) == deviceName && resource->name_ == name)
            return resource;
    }
    return nullptr;
}

NetResource *ResourcesManager::findResourceByName(const String &name, bool &ambiguous) const
{
    ambiguous = false;
    NetResource *match = nullptr;
    for (int i = 0; i < resourceCount_; ++i)
    {
        NetResource *resource = resources_[i];
        if (resource->name_ != name)
            continue;
        if (match != nullptr)
        {
            ambiguous = true;
            return nullptr;
        }
        match = resource;
    }
    return match;
}

NetResource *ResourcesManager::findCommandResource(const String &address, bool &ambiguous) const
{
    ambiguous = false;
    const int slash = address.indexOf('/');
    if (slash < 0)
        return findResourceByName(address, ambiguous);
    if (slash == 0 || slash == static_cast<int>(address.length()) - 1 ||
        address.indexOf('/', slash + 1) >= 0)
        return nullptr;

    const String owner = address.substring(0, slash);
    const String name = address.substring(slash + 1);
    if (!DeviceIdentity::validDeviceName(owner) || !validSegment(name))
        return nullptr;
    return findResource(owner, name);
}

// Unlike findResource(), this cannot be fooled by `self` appearing first.
bool ResourcesManager::addressTakenByOther(const String &deviceName, const String &name,
                                           const NetResource *self) const
{
    for (int i = 0; i < resourceCount_; ++i)
    {
        const NetResource *resource = resources_[i];
        if (resource != self && resolveResourceOwner(*resource) == deviceName &&
            resource->name_ == name)
            return true;
    }
    return false;
}

bool ResourcesManager::bindResource(NetResource *resource)
{
    if (resource == nullptr || resource->resourceManager_ != nullptr ||
        resourceCount_ >= MaxResources)
        return false;
    if (resource->kind_ == NetResourceType::ACTION &&
        !validActionSchema(*static_cast<NetActionResource *>(resource)))
    {
        LOG_ERROR("RM", "Cannot bind action '%s': invalid argument schema", resource->name_.c_str());
        return false;
    }

    const String &thisDevice = gDeviceIdentity.getDeviceName();
    if (resource->isOwned())
    {
        if (!validSegment(resource->name_) || !DeviceIdentity::validDeviceName(thisDevice))
        {
            LOG_ERROR("RM", "Cannot bind managed resource '%s': invalid name or local identity",
                      resource->name_.c_str());
            return false;
        }
        gDeviceIdentity.lockAddress();
    }
    else if (sourceConfigured(*resource))
    {
        // Configured, so it has to be a usable address: a present-but-invalid
        // source is a mistake, unlike one that simply has not been set yet.
        if (!hasResolvedSource(*resource) || resource->ownerDevice_.deviceName == thisDevice)
        {
            LOG_ERROR("RM", "Cannot bind remote resource '%s' for device '%s': invalid source",
                      resource->name_.c_str(), resource->ownerDevice_.deviceName.c_str());
            return false;
        }
    }

    // An unconfigured remote resource has no logical address yet, so it cannot
    // collide with anything.
    if (hasResolvedSource(*resource) &&
        findResource(resolveResourceOwner(*resource), resource->name_) != nullptr)
    {
        LOG_ERROR("RM", "Cannot bind resource '%s' for device '%s': already bound",
                  resource->name_.c_str(), resolveResourceOwner(*resource).c_str());
        return false;
    }

    const bool firstFromOwner = !resource->isOwned() && hasResolvedSource(*resource) &&
                                !remoteOwnerInUse(resource->ownerDevice_.deviceName, resource);
    resource->resourceManager_ = this;
    resources_[resourceCount_++] = resource;
    LOG("RM", "Bound %s '%s' (%s) for device '%s'", kindName(resource->kind_),
        resource->name_.c_str(), resource->isOwned() ? "managed" : "remote",
        resolveResourceOwner(*resource).c_str());

    subscribeResource(*resource, firstFromOwner);

    if (resource->isOwned())
    {
        publishManifest();
        if (resource->kind_ == NetResourceType::VALUE)
            publishState(*static_cast<NetValueResource *>(resource));
    }
    else
        publishConsumeManifest();
    return true;
}

void ResourcesManager::unbindResource(NetResource *resource)
{
    if (resource == nullptr)
        return;
    for (int i = 0; i < resourceCount_; ++i)
    {
        if (resources_[i] != resource)
            continue;

        const bool managed = resource->isOwned();
        // Drop the retained state this device put on the broker.
        if (managed && resource->kind_ == NetResourceType::VALUE && publisher_ != nullptr &&
            static_cast<NetValueResource *>(resource)->hasAuthoritativeValue_ &&
            hasResolvedSource(*resource))
            publisher_->publish(resolveResourceTopic(*resource, ResourceTopicOperation::STATE),
                                String(), true);

        for (int j = i; j < resourceCount_ - 1; ++j)
            resources_[j] = resources_[j + 1];
        resources_[--resourceCount_] = nullptr;
        resource->resourceManager_ = nullptr;

        // Now that it is out of the registry, "last one for this owner" is an
        // honest question to ask.
        unsubscribeResource(*resource,
                            !remoteOwnerInUse(resource->ownerDevice_.deviceName, resource));
        if (managed)
            publishManifest();
        else
            publishConsumeManifest();
        return;
    }
}

void ResourcesManager::notifySourceChanged(NetResource &resource, const NetDeviceIdentity &oldOwner,
                                           const String &oldName)
{
    if (resource.resourceManager_ != this || resource.isOwned())
        return;

    const bool ownerChanged = oldOwner.deviceName != resource.ownerDevice_.deviceName;

    if (subscriber_ != nullptr)
    {
        const String oldIngress = ingressTopicFor(resource, oldOwner.deviceName, oldName);
        if (oldIngress.length() != 0)
            subscriber_->unsubscribe(oldIngress);
        // The resource already carries the new owner, so it no longer counts
        // towards the old one.
        if (ownerChanged && DeviceIdentity::validDeviceName(oldOwner.deviceName) &&
            !remoteOwnerInUse(oldOwner.deviceName, nullptr))
            subscriber_->unsubscribe(
                resolveResourceManifestTopic(oldOwner.deviceName, ManifestFormat::MSGPACK));
    }

    if (!sourceConfigured(resource))
    {
        publishConsumeManifest();
        return; // Registered, but not pointed at anything yet.
    }

    // setSource() cannot fail, so a target that is unusable or would make
    // routing ambiguous is refused by detaching the resource instead: with no
    // owner it matches no topic, stays registered, and can be pointed somewhere
    // valid later. Left addressable, it could shadow the resource that holds
    // that address legitimately, since ingress routes to the first match.
    const String newOwner = resource.ownerDevice_.deviceName;
    const char *refusal = nullptr;
    if (!hasResolvedSource(resource))
        refusal = "not a valid device/resource address";
    else if (newOwner == gDeviceIdentity.getDeviceName())
        refusal = "it names this device";
    else if (addressTakenByOther(newOwner, resource.name_, &resource))
        refusal = "another bound resource already represents it";
    if (refusal != nullptr)
    {
        LOG_ERROR("RM", "Refused source '%s/%s' for remote resource: %s",
                  newOwner.c_str(), resource.name_.c_str(), refusal);
        resource.ownerDevice_.deviceName = String();
        publishConsumeManifest();
        return;
    }

    if (subscriber_ != nullptr)
    {
        if (verifiesRemoteManifests() && ownerChanged && !remoteOwnerInUse(newOwner, &resource))
            subscriber_->subscribe(
                resolveResourceManifestTopic(newOwner, ManifestFormat::MSGPACK));
        const String ingress = ingressTopicFor(resource);
        if (ingress.length() != 0)
            subscriber_->subscribe(ingress);
    }
    publishConsumeManifest();
}

bool ResourcesManager::publishManifest()
{
    const String &thisDevice = gDeviceIdentity.getDeviceName();
    if (publisher_ == nullptr || !DeviceIdentity::validDeviceName(thisDevice))
        return false;

    // Both encodings, both retained. A reader takes whichever it can decode and
    // nothing has to negotiate a format. The JSON one is published first and is
    // the one whose failure fails this call: it is the manifest the protocol
    // has always defined, and a device that could not publish it has not
    // announced itself. A MessagePack failure is logged and tolerated, so an
    // older reader is never held back by the compact form.
    {
        String json;
        if (!serializeManifest(json, ManifestFormat::JSON))
            return false;
        if (!publisher_->publish(resolveResourceManifestTopic(thisDevice, ManifestFormat::JSON), json,
                                 true))
            return false;
    }
    {

        String packed;
        if (!serializeManifest(packed, ManifestFormat::MSGPACK) ||
            !publisher_->publish(resolveResourceManifestTopic(thisDevice, ManifestFormat::MSGPACK),
                                 packed, true))
            LOG_WARNING("RM", "Published the JSON manifest but not the MessagePack one");
    }
    return true;
}

bool ResourcesManager::publishManifest(ManifestFormat format)
{
    const String &thisDevice = gDeviceIdentity.getDeviceName();
    if (publisher_ == nullptr || !DeviceIdentity::validDeviceName(thisDevice))
        return false;
    String payload;
    return serializeManifest(payload, format) &&
           publisher_->publish(resolveResourceManifestTopic(thisDevice, format), payload, true);
}

bool ResourcesManager::publishConsumeManifest()
{
    const String &thisDevice = gDeviceIdentity.getDeviceName();
    if (publisher_ == nullptr || !DeviceIdentity::validDeviceName(thisDevice))
        return false;

    String json;
    if (!serializeConsumeManifest(json, ManifestFormat::JSON) ||
        !publisher_->publish(resolveResourceConsumeManifestTopic(thisDevice), json, true))
        return false;

    String packed;
    if (!serializeConsumeManifest(packed, ManifestFormat::MSGPACK) ||
        !publisher_->publish(resolveResourceConsumeManifestTopic(thisDevice,
                                                                 ManifestFormat::MSGPACK),
                             packed, true))
        LOG_WARNING("RM", "Published the JSON consume manifest but not the MessagePack one");
    return true;
}

bool ResourcesManager::publishConsumeManifest(ManifestFormat format)
{
    const String &thisDevice = gDeviceIdentity.getDeviceName();
    if (publisher_ == nullptr || !DeviceIdentity::validDeviceName(thisDevice))
        return false;
    String payload;
    return serializeConsumeManifest(payload, format) &&
           publisher_->publish(resolveResourceConsumeManifestTopic(thisDevice, format),
                               payload, true);
}

// The JSON manifest, unchanged since the protocol first defined it: named keys,
// enum names spelled out. That topic is read by people and by tools that have
// never seen this header, and it is the fallback for anyone who cannot decode
// the compact form, so it stays exactly as it is.
void ResourcesManager::buildNamedManifest(JsonDocument &doc) const
{
    doc["version"] = ManifestVersion;
    JsonArray items = doc["resources"].to<JsonArray>();
    for (int i = 0; i < resourceCount_; ++i)
    {
        const NetResource &resource = *resources_[i];
        if (!resource.isOwned())
            continue; // Only what this device implements.
        JsonObject item = items.add<JsonObject>();
        item["name"] = resource.name_;
        item["kind"] = kindName(resource.kind_);
        if (resource.kind_ == NetResourceType::VALUE)
        {
            const NetValueResource &value = static_cast<const NetValueResource &>(resource);
            item["access"] = accessName(value.access_);
            item["type"] = valueTypeName(value.valueType_);
        }
        else
        {
            // Published whether or not payloads are checked: this is the
            // action describing itself.
            const NetActionResource &action = static_cast<const NetActionResource &>(resource);
            JsonArray args = item["arguments"].to<JsonArray>();
            for (size_t a = 0; a < action.argumentCount(); ++a)
            {
                JsonObject argument = args.add<JsonObject>();
                argument["name"] = action.argument(a).name;
                argument["type"] = valueTypeName(action.argument(a).type);
                argument["required"] = action.argument(a).required;
            }
        }
    }
}

// The compact manifest: positions instead of keys, enum values instead of
// names. The layout and the rules for changing it live in NetResources.h and
// are not restated here -- there must be one description of a wire format.
void ResourcesManager::buildPositionalManifest(JsonDocument &doc) const
{
    JsonArray root = doc.to<JsonArray>();
    root.add(ManifestEncodingVersion); // Position 0, frozen for all time.
    root.add(ManifestVersion);
    JsonArray items = root.add<JsonArray>();

    for (int i = 0; i < resourceCount_; ++i)
    {
        const NetResource &resource = *resources_[i];
        if (!resource.isOwned())
            continue;
        JsonArray item = items.add<JsonArray>();
        // Kind first: it tells a reader which shape the rest of this array is.
        item.add(static_cast<uint8_t>(resource.kind_));
        item.add(resource.name_);

        if (resource.kind_ == NetResourceType::VALUE)
        {
            const NetValueResource &value = static_cast<const NetValueResource &>(resource);
            item.add(static_cast<uint8_t>(value.access_));
            item.add(static_cast<uint8_t>(value.valueType_));
            continue;
        }

        const NetActionResource &action = static_cast<const NetActionResource &>(resource);
        JsonArray args = item.add<JsonArray>();
        for (size_t a = 0; a < action.argumentCount(); ++a)
        {
            JsonArray argument = args.add<JsonArray>();
            argument.add(action.argument(a).name);
            argument.add(static_cast<uint8_t>(action.argument(a).type));
            argument.add(action.argument(a).required);
        }
    }
}

void ResourcesManager::buildNamedConsumeManifest(JsonDocument &doc) const
{
    doc["version"] = ConsumeManifestVersion;
    JsonArray consumes = doc["consumes"].to<JsonArray>();
    for (int i = 0; i < resourceCount_; ++i)
    {
        const NetResource &resource = *resources_[i];
        if (resource.isOwned() || !hasResolvedSource(resource))
            continue;
        JsonObject item = consumes.add<JsonObject>();
        item["device"] = resource.ownerDevice_.deviceName;
        item["resource"] = resource.name_;
        item["kind"] = kindName(resource.kind_);
        if (resource.kind_ == NetResourceType::VALUE)
        {
            const NetValueResource &value = static_cast<const NetValueResource &>(resource);
            item["access"] = accessName(value.access_);
            item["type"] = valueTypeName(value.valueType_);
        }
        else
        {
            const NetActionResource &action = static_cast<const NetActionResource &>(resource);
            JsonArray args = item["arguments"].to<JsonArray>();
            for (size_t a = 0; a < action.argumentCount(); ++a)
            {
                JsonObject argument = args.add<JsonObject>();
                argument["name"] = action.argument(a).name;
                argument["type"] = valueTypeName(action.argument(a).type);
                argument["required"] = action.argument(a).required;
            }
        }
    }
}

void ResourcesManager::buildPositionalConsumeManifest(JsonDocument &doc) const
{
    JsonArray root = doc.to<JsonArray>();
    root.add(ConsumeManifestEncodingVersion);
    root.add(ConsumeManifestVersion);
    JsonArray consumes = root.add<JsonArray>();
    for (int i = 0; i < resourceCount_; ++i)
    {
        const NetResource &resource = *resources_[i];
        if (resource.isOwned() || !hasResolvedSource(resource))
            continue;
        JsonArray item = consumes.add<JsonArray>();
        item.add(static_cast<uint8_t>(resource.kind_));
        item.add(resource.ownerDevice_.deviceName);
        item.add(resource.name_);
        if (resource.kind_ == NetResourceType::VALUE)
        {
            const NetValueResource &value = static_cast<const NetValueResource &>(resource);
            item.add(static_cast<uint8_t>(value.access_));
            item.add(static_cast<uint8_t>(value.valueType_));
        }
        else
        {
            const NetActionResource &action = static_cast<const NetActionResource &>(resource);
            JsonArray args = item.add<JsonArray>();
            for (size_t a = 0; a < action.argumentCount(); ++a)
            {
                JsonArray argument = args.add<JsonArray>();
                argument.add(action.argument(a).name);
                argument.add(static_cast<uint8_t>(action.argument(a).type));
                argument.add(action.argument(a).required);
            }
        }
    }
}

bool ResourcesManager::serializeManifest(String &payload, ManifestFormat format) const
{
    // ArduinoJson 7 documents size themselves, growing through a chain of small
    // pools instead of one block fixed at construction. That deletes the
    // question this function used to have to answer -- how big a manifest is
    // about to be -- and with it the 16KB contiguous request that answer
    // defaulted to. It also removes the failure mode: there is no capacity to
    // overflow, so a manifest can no longer be silently dropped for being
    // larger than a guess made before it was built.
    JsonDocument doc;
    const bool packed = format == ManifestFormat::MSGPACK;
    if (packed)
        buildPositionalManifest(doc);
    else
        buildNamedManifest(doc);

    // All or nothing. A manifest is retained, so a short one is not a failure
    // that gets retried -- it is a lie that stays on the broker, and every
    // reader that later asks what this device offers is told the truncated
    // answer. Refusing leaves the previous manifest up, which is at worst out
    // of date rather than wrong.
    const size_t measured = packed ? measureMsgPack(doc) : measureJson(doc);
    const PayloadResult outcome = serializeWholeDocument(
        doc, packed ? DocumentEncoding::MSGPACK : DocumentEncoding::JSON, payload);
    if (outcome != PayloadResult::Complete)
    {
        LOG_WARNING("RM", "Not publishing the %s manifest (%u bytes): %s "
                          "(8bit heap free=%u largest=%u)",
                    packed ? "MessagePack" : "JSON", (unsigned)measured,
                    describePayloadResult(outcome),
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        return false;
    }

    // The protocol ceiling is still enforced, just on the finished document
    // rather than on a guess made before building it: a manifest too large to
    // publish is a real condition, a pool too small to build one is not.
    if (payload.length() > MaxManifestLength)
    {
        LOG_WARNING("RM", "Manifest is %u bytes, over the %u-byte ceiling; publishing nothing",
                    (unsigned)payload.length(), (unsigned)MaxManifestLength);
        payload = String();
        return false;
    }
    return true;
}

bool ResourcesManager::serializeConsumeManifest(String &payload, ManifestFormat format) const
{
    JsonDocument doc;
    const bool packed = format == ManifestFormat::MSGPACK;
    if (packed)
        buildPositionalConsumeManifest(doc);
    else
        buildNamedConsumeManifest(doc);
    if (serializeWholeDocument(doc, packed ? DocumentEncoding::MSGPACK : DocumentEncoding::JSON,
                               payload) != PayloadResult::Complete ||
        payload.length() > MaxManifestLength)
    {
        payload = String();
        return false;
    }
    return true;
}

bool ResourcesManager::publishState(const NetValueResource &resource)
{
    if (publisher_ == nullptr || !resource.isOwned() || !resource.hasAuthoritativeValue_ ||
        !hasResolvedSource(resource))
        return false;
    // The wire format is the codec's own representation: "23.5", "true", raw
    // string. The declared type is already in the manifest. Empty would read as
    // a deletion, and the size limit holds however the value got here.
    const String encoded = resource.encodedValue();
    if (encoded.length() == 0 || encoded.length() > MaxValueLength)
    {
        LOG_WARNING("RM", "Not publishing '%s': encoded length %u is outside 1..%u",
                    resource.name_.c_str(), (unsigned)encoded.length(), (unsigned)MaxValueLength);
        return false;
    }
    return publisher_->publish(resolveResourceTopic(resource, ResourceTopicOperation::STATE),
                               encoded, true);
}

ActionResult ResourcesManager::listResources() const
{
    auto displayName = [](const NetResource &resource) -> String
    {
        return resource.name_.length() == 0 ? String("<unnamed>") : resource.name_;
    };
    auto shortNameIsAmbiguous = [this](const NetResource &candidate) -> bool
    {
        if (candidate.name_.length() == 0)
            return false;
        int matches = 0;
        for (int i = 0; i < resourceCount_; ++i)
            if (resources_[i]->name_ == candidate.name_ && ++matches > 1)
                return true;
        return false;
    };
    auto displayValue = [&shortNameIsAmbiguous](const NetResource &resource) -> String
    {
        String value;
        if (resource.kind_ == NetResourceType::ACTION)
            value = "-";
        else
        {
            const NetValueResource &typed = static_cast<const NetValueResource &>(resource);
            value = typed.hasValueImpl() ? typed.encodedCurrentValue()
                                         : String("<unavailable>");
            value.replace("\r", "\\r");
            value.replace("\n", "\\n");
            value.replace("\t", "\\t");
        }
        if (!resource.isOwned() && shortNameIsAmbiguous(resource) &&
            resource.ownerDevice_.deviceName.length() != 0)
        {
            value += " @";
            value += resource.ownerDevice_.deviceName;
        }
        return value;
    };

    size_t typeWidth = 4;
    size_t nameWidth = 4;
    for (int i = 0; i < resourceCount_; ++i)
    {
        const NetResource &resource = *resources_[i];
        const size_t typeLength = strlen(kindName(resource.kind_));
        const size_t nameLength = displayName(resource).length();
        if (typeLength > typeWidth)
            typeWidth = typeLength;
        if (nameLength > nameWidth)
            nameWidth = nameLength;
    }

    String response;
    auto appendColumn = [&response](const String &text, size_t width)
    {
        response += text;
        for (size_t i = text.length(); i < width; ++i)
            response += ' ';
    };
    appendColumn("TYPE", typeWidth);
    response += "  ";
    appendColumn("NAME", nameWidth);
    response += "  VALUE";
    for (int i = 0; i < resourceCount_; ++i)
    {
        const NetResource &resource = *resources_[i];
        response += '\n';
        appendColumn(kindName(resource.kind_), typeWidth);
        response += "  ";
        appendColumn(displayName(resource), nameWidth);
        response += "  ";
        response += displayValue(resource);
        if (response.length() > MaxManifestLength)
            return {false, String("Resource list too large")};
    }
    return {true, response};
}

bool ResourcesManager::announceAll()
{
    bool published = publishManifest();
    if (!publishConsumeManifest())
        published = false;
    for (int i = 0; i < resourceCount_; ++i)
    {
        NetResource *resource = resources_[i];
        if (!resource->isOwned() || resource->kind_ != NetResourceType::VALUE)
            continue;
        NetValueResource &value = *static_cast<NetValueResource *>(resource);
        if (value.hasAuthoritativeValue_ && !publishState(value))
            published = false;
    }
    return published;
}

bool ResourcesManager::withdrawIdentity(const String &oldDeviceName)
{
    // Never the running identity: that data is live, not leftover.
    if (publisher_ == nullptr || !DeviceIdentity::validDeviceName(oldDeviceName) ||
        oldDeviceName == gDeviceIdentity.getDeviceName())
        return false;

    // Explicit old addresses; the resources themselves keep the current identity.
    bool withdrawn = true;
    for (int i = 0; i < resourceCount_; ++i)
    {
        const NetResource &resource = *resources_[i];
        if (!resource.isOwned() || resource.kind_ != NetResourceType::VALUE ||
            !validSegment(resource.name_))
            continue;
        if (!publisher_->publish(resolveResourceTopic(oldDeviceName, resource.name_,
                                                      ResourceTopicOperation::STATE),
                                 String(), true))
            withdrawn = false;
    }
    // Actions need nothing: /invoke is never retained.
    //
    // Both manifests go, not just the JSON one -- either left behind would
    // re-announce the old identity to whichever reader prefers that encoding.
    if (!publisher_->publish(resolveResourceManifestTopic(oldDeviceName, ManifestFormat::JSON),
                             String(), true))
        withdrawn = false;
    if (!publisher_->publish(resolveResourceManifestTopic(oldDeviceName, ManifestFormat::MSGPACK),
                             String(), true))
        withdrawn = false;
    if (!publisher_->publish(resolveResourceConsumeManifestTopic(oldDeviceName), String(), true))
        withdrawn = false;
    if (!publisher_->publish(resolveResourceConsumeManifestTopic(oldDeviceName,
                                                                 ManifestFormat::MSGPACK),
                             String(), true))
        withdrawn = false;
    return withdrawn;
}

bool ResourcesManager::setValue(NetValueResource &resource, const String &encoded)
{
    if (resource.resourceManager_ != this || encoded.length() == 0 ||
        encoded.length() > MaxValueLength)
        return false;

    if (!resource.isOwned())
    {
        // A remote write only counts once the transport took it; otherwise the
        // caller would start an optimistic window over a request nobody sent.
        if (resource.access_ != AccessPolicy::READ_WRITE || publisher_ == nullptr ||
            !hasResolvedSource(resource))
            return false;
        return publisher_->publish(resolveResourceTopic(resource, ResourceTopicOperation::SET),
                                   encoded, false);
    }

    // Local truth does not depend on the network: publish best-effort and let
    // the caller commit regardless. announceAll() retries after a reconnect.
    if (publisher_ != nullptr && hasResolvedSource(resource))
        publisher_->publish(resolveResourceTopic(resource, ResourceTopicOperation::STATE),
                            encoded, true);
    return true;
}

bool ResourcesManager::invoke(NetActionResource &resource, const String &payload)
{
    if (resource.resourceManager_ != this || payload.length() > MaxValueLength)
        return false;
    // Only a remote action is dispatched; a managed one is executed instead.
    if (resource.isOwned() || !hasResolvedSource(resource) || publisher_ == nullptr)
        return false;
#if NM_ENABLE_ACTION_PAYLOAD_ASSERTION
    // Only against a schema this caller chose to declare. None is the normal
    // case and is never itself a failure, nor is a manifest never received.
    if (resource.hasSchema())
    {
        String error;
        if (!assertActionPayload(resource, payload, error))
        {
            LOG_WARNING("RM", "Not invoking '%s/%s': %s", resource.ownerDevice_.deviceName.c_str(),
                        resource.name_.c_str(), error.c_str());
            return false;
        }
    }
#endif
    return publisher_->publish(resolveResourceTopic(resource, ResourceTopicOperation::INVOKE),
                               payload, false);
}

ActionResult ResourcesManager::executeAction(NetActionResource &action, const String &canonicalPayload)
{
    if (!action.isOwned())
        return {false, String("not implemented by this device")};
    if (canonicalPayload.length() > MaxValueLength)
        return {false, String("payload too large")};
#if NM_ENABLE_ACTION_PAYLOAD_ASSERTION
    // A managed action always has a declaration, even an empty one: zero
    // arguments still means "an empty payload or a JSON object".
    String error;
    if (!assertActionPayload(action, canonicalPayload, error))
        return {false, error};
#endif
    return action.execute(canonicalPayload);
}

ActionResult ResourcesManager::executeCommand(const String &expression)
{
    LOG("RM", "Executing command: '%s'", expression.c_str());
    const ParsedCommand command = parseCommand(expression);
    if (expression.length() == 0)
        return {false, String("Usage: >list | >manifest | >drop <name|owner/name> | >raw <topic> [payload] | > <name|owner/name> [verb [payload]]")};

    if (command.internalSyntax)
    {
        if (command.internalCommand == InternalCommands::LIST)
        {
            String arguments = command.payload;
            arguments.trim();
            if (arguments.length() != 0)
                return {false, String("Usage: >list")};
            return listResources();
        }

        if (command.internalCommand == InternalCommands::MANIFEST)
        {
            // Binary MessagePack is not useful through a text response. Asking
            // for it republishes the retained compact manifest instead.
            String arguments = command.payload;
            arguments.trim();
            size_t position = 0;
            String first = commandToken(arguments, position);
            String firstUpper = first;
            firstUpper.toUpperCase();
            const bool explicitPublish = firstUpper == "PUBLISH";
            String formatArgument = explicitPublish ? commandToken(arguments, position) : first;
            String extra = position < arguments.length() ? arguments.substring(position) : String();
            extra.trim();
            if (extra.length() != 0)
                return {false, String("Usage: >manifest [publish] [json|msgpack]")};

            if (explicitPublish && formatArgument.length() == 0)
            {
                const bool published = publishManifest();
                return {published, published ? String("Republished to MQTT.")
                                             : String("Manifest publish failed.")};
            }

            ManifestFormat format = ManifestFormat::JSON;
            if (!parseManifestFormat(formatArgument, format))
                return {false, String("Usage: >manifest [publish] [json|msgpack]")};

            if (explicitPublish || format == ManifestFormat::MSGPACK)
            {
                const bool published = publishManifest(format);
                return {published, published ? String("Republished to MQTT.")
                                             : String("Manifest publish failed.")};
            }

            String manifest;
            if (!serializeManifest(manifest, format))
                return {false, String("Could not serialize manifest")};
            return {true, manifest};
        }

        if (command.internalCommand == InternalCommands::DROP)
        {
            size_t position = 0;
            const String name = commandToken(command.payload, position);
            String extra = position < command.payload.length()
                               ? command.payload.substring(position)
                               : String();
            extra.trim();
            if (name.length() == 0 || extra.length() != 0)
                return {false, String("Usage: >drop <name|owner/name>")};

            bool ambiguous = false;
            NetResource *resource = findCommandResource(name, ambiguous);
            if (ambiguous)
                return {false, String("Resource name is ambiguous; use owner/name")};
            if (resource == nullptr)
                return {false, String("Resource not found")};

            unbindResource(resource);
            return {true, String("Dropped resource: ") + name};
        }

        if (command.internalCommand == InternalCommands::RAW)
        {
            if (command.target.length() == 0)
                return {false, String("Usage: >raw <topic> [payload]")};
            const bool consumed = handleIngressMessage(command.target, command.payload);
            return {consumed, consumed ? String("OK") : String("Resource topic not consumed")};
        }

        return {false, String("Unknown resource-manager action: ") + command.internalAction};
    }

    if (command.target.length() == 0)
        return {false, String("Usage: > <name|owner/name> [get|set|invoke] [payload]")};

    auto invokeAction = [this](NetActionResource &action, const String &payload) -> ActionResult
    {
        if (action.isOwned())
        {
            ActionResult result = executeAction(action, payload);
            if (result.result.length() == 0)
                result.result = result.success ? String("OK") : String("Action failed");
            return result;
        }
        const bool accepted = invoke(action, payload);
        return {accepted, accepted ? String("OK") : String("Could not publish action")};
    };

    bool ambiguous = false;
    NetResource *resource = findCommandResource(command.target, ambiguous);
    if (ambiguous)
        return {false, String("Resource name is ambiguous; use owner/name")};
    if (resource == nullptr)
        return {false, String("Resource not found")};

    String verb = command.verb;
    verb.toUpperCase();
    if (verb.length() == 0)
    {
        if (resource->kind_ == NetResourceType::ACTION)
            return invokeAction(*static_cast<NetActionResource *>(resource), String());

        NetValueResource &value = *static_cast<NetValueResource *>(resource);
        if (!value.hasValueImpl())
            return {false, String("Value unavailable")};
        return {true, value.encodedCurrentValue()};
    }

    if (verb == "GET")
    {
        if (resource->kind_ != NetResourceType::VALUE)
            return {false, String("GET requires a value resource")};
        String unexpected = command.payload;
        unexpected.trim();
        if (unexpected.length() != 0)
            return {false, String("GET does not accept a payload")};

        NetValueResource &value = *static_cast<NetValueResource *>(resource);
        if (!value.hasValueImpl())
            return {false, String("Value unavailable")};
        return {true, value.encodedCurrentValue()};
    }

    if (verb == "SET")
    {
        if (resource->kind_ != NetResourceType::VALUE)
            return {false, String("SET requires a value resource")};
        if (command.payload.length() == 0)
            return {false, String("SET requires a payload")};
        if (command.payload.length() > MaxValueLength)
            return {false, String("Payload too large")};

        NetValueResource &value = *static_cast<NetValueResource *>(resource);
        if (value.isOwned())
        {
            if (!applyManagedWrite(value, command.payload))
                return {false, String("Value write rejected")};
            return {true, value.encodedCurrentValue()};
        }
        const bool accepted = value.requestEncodedValue(command.payload);
        return {accepted, accepted ? String("OK") : String("Could not publish value")};
    }

    if (verb != "INVOKE" && verb != "ACTION")
        return {false, String("Unknown resource verb: ") + command.verb};
    if (resource->kind_ != NetResourceType::ACTION)
        return {false, String("INVOKE requires an action resource")};

    if (command.payload.length() > MaxValueLength)
        return {false, String("Payload too large")};
    return invokeAction(*static_cast<NetActionResource *>(resource), command.payload);
}

bool ResourcesManager::applyRemoteState(NetValueResource &value, const String &message)
{
    if (message.length() == 0)
    {
        // The retained state was deleted. The last known value stays readable;
        // only setSource() discards it, because only that changes what the
        // resource represents.
        value.freshness_ = ResourceFreshness::STALE;
        return true;
    }
    if (message.length() > MaxValueLength)
    {
        LOG_WARNING("RM", "Ignored oversized state for '%s/%s' (%u bytes)",
                    value.ownerDevice_.deviceName.c_str(), value.name_.c_str(),
                    (unsigned)message.length());
        return false;
    }
    // The decode belongs to NetValue<T>/NetCodec<T>, never here.
    if (!value.applyEncodedOwnerValue(message))
    {
        LOG_WARNING("RM", "Ignored undecodable state for '%s/%s'",
                    value.ownerDevice_.deviceName.c_str(), value.name_.c_str());
        return false;
    }
    return true;
}

bool ResourcesManager::applyManagedWrite(NetValueResource &value, const String &message)
{
    if (value.access_ != AccessPolicy::READ_WRITE)
    {
        LOG_WARNING("RM", "Ignored write to read-only '%s'", value.name_.c_str());
        return false;
    }
    if (message.length() > MaxValueLength)
    {
        LOG_WARNING("RM", "Ignored oversized write to '%s' (%u bytes)", value.name_.c_str(),
                    (unsigned)message.length());
        return false;
    }
    // Rejected or undecodable: state is unchanged, so there is nothing to publish.
    if (!value.applyEncodedWrite(message))
    {
        LOG("RM", "Write to '%s' was not applied", value.name_.c_str());
        return false;
    }
    publishState(value);
    return true;
}

// The JSON manifest, for a handler that asked for it. Nothing in the library
// reads this topic any more -- verification moved to the compact encoding -- so
// this path exists to hand a validated payload to a consumer and does nothing
// else with it.
//
// A manifest is description: it feeds discovery and compatibility diagnostics
// and nothing else. Value freshness belongs to /state alone, so no outcome here
// (missing, mismatched, withdrawn or malformed) touches it, and nothing here
// gates /state, /set or /invoke. Whether the message belongs to this Manager is
// decided by handleIngressMessage(), not here.
void ResourcesManager::applyOtherDeviceManifest(const String &deviceName, const String &message)
{
    if (message.length() > MaxManifestLength)
    {
        LOG_WARNING("RM", "Ignored oversized manifest from '%s' (%u bytes)", deviceName.c_str(),
                    (unsigned)message.length());
        return;
    }

    // An empty retained manifest withdraws the declarations, and nothing more:
    // values keep whatever freshness their own /state gave them. Discovery
    // consumers still need to hear about it, so it counts as valid.
    if (message.length() == 0)
    {
        if (manifestHandler_ != nullptr)
            manifestHandler_(deviceName, message);
        return;
    }

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, message);
    // Running out of room is reported apart from bad data on purpose. They have
    // nothing to do with each other -- one is this device's problem and the
    // other is the sender's -- and a single "malformed" for both sends whoever
    // reads the log to inspect a manifest that was perfectly good.
    //
    // Under ArduinoJson 7 this is a genuine out-of-memory rather than a pool
    // guessed too small: the document grows in small pools as it parses, so
    // reaching here means the heap could not spare even those. The heap is read
    // where the failure happens, on the MQTT task in the middle of the connect
    // burst, because by the time any periodic sample looks the moment is gone.
    if (error == DeserializationError::NoMemory)
    {
        LOG_WARNING("RM", "Out of memory parsing the manifest from '%s' (%u bytes of JSON; "
                          "8bit heap free=%u largest=%u)",
                    deviceName.c_str(), (unsigned)message.length(),
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        return;
    }
    if (error || !doc["resources"].is<JsonArray>())
    {
        // Not valid data, so the handler, which is promised only valid data, is not called.
        LOG_WARNING("RM", "Ignored malformed manifest from '%s'", deviceName.c_str());
        return;
    }

    manifestHandler_(deviceName, message);
}

#if NM_ENABLE_REMOTE_RESOURCE_VERIFICATION
// Positional throughout: entry[0] kind, entry[1] name, then the shape kind
// chose. Rule 4 governs the enum comparisons -- a value this build does not
// recognise is reported as the number it is, never translated through a name
// table that would quietly read it as something else.
void ResourcesManager::verifyAgainstManifest(const String &deviceName, JsonArrayConst items)
{
    for (int i = 0; i < resourceCount_; ++i)
    {
        NetResource *resource = resources_[i];
        if (resource->isOwned() || resource->ownerDevice_.deviceName != deviceName)
            continue;

        JsonArrayConst declaration;
        for (JsonVariantConst element : items)
        {
            JsonArrayConst entry = element.as<JsonArrayConst>();
            if (entry.size() < 2)
                continue;
            const char *name = entry[1].as<const char *>();
            if (name != nullptr && resource->name_ == name)
            {
                declaration = entry;
                break;
            }
        }

        // The local declaration is intentional and is never rewritten from a
        // remote manifest. A disagreement is reported, and that is all.
        const bool declared =
            declaration.size() >= 2 &&
            declaration[0].as<uint8_t>() == static_cast<uint8_t>(resource->kind_);

        if (resource->kind_ == NetResourceType::VALUE)
        {
            const NetValueResource &value = *static_cast<NetValueResource *>(resource);
            bool compatible = declared && declaration.size() >= 4 &&
                              declaration[3].as<uint8_t>() ==
                                  static_cast<uint8_t>(value.valueType_);
            // Asymmetric on purpose: reading a read_write resource is fine,
            // writing a read-only one is not.
            if (compatible && value.access_ == AccessPolicy::READ_WRITE &&
                declaration[2].as<uint8_t>() !=
                    static_cast<uint8_t>(AccessPolicy::READ_WRITE))
                compatible = false;
            if (!compatible)
                LOG_WARNING("RM",
                            "Source '%s/%s' is missing or incompatible with the local declaration",
                            deviceName.c_str(), resource->name_.c_str());
            continue;
        }

        // For an action the manifest is description, never a gate: invoke()
        // keeps working however this comparison turns out.
        if (!declared)
        {
            LOG_WARNING("RM", "Action '%s/%s' is not declared by its device", deviceName.c_str(),
                        resource->name_.c_str());
            continue;
        }
        const NetActionResource &action = *static_cast<NetActionResource *>(resource);
        if (!action.hasSchema())
            continue;
        JsonArrayConst remoteArgs =
            declaration.size() >= 3 ? declaration[2].as<JsonArrayConst>() : JsonArrayConst();
        for (size_t a = 0; a < action.argumentCount(); ++a)
        {
            const ActionArgMetadata &expected = action.argument(a);
            bool found = false;
            for (JsonVariantConst argumentElement : remoteArgs)
            {
                JsonArrayConst argument = argumentElement.as<JsonArrayConst>();
                if (argument.size() < 2)
                    continue;
                const char *name = argument[0].as<const char *>();
                if (name == nullptr || strcmp(name, expected.name) != 0)
                    continue;
                found = true;
                const uint8_t remoteType = argument[1].as<uint8_t>();
                if (remoteType != static_cast<uint8_t>(expected.type))
                    LOG_WARNING("RM", "Action '%s/%s' argument '%s' is type %u remotely, expected %s",
                                deviceName.c_str(), resource->name_.c_str(), expected.name,
                                (unsigned)remoteType, valueTypeName(expected.type));
                break;
            }
            if (!found)
                LOG_WARNING("RM", "Action '%s/%s' does not declare expected argument '%s'",
                            deviceName.c_str(), resource->name_.c_str(), expected.name);
        }
        for (JsonVariantConst argumentElement : remoteArgs)
        {
            JsonArrayConst argument = argumentElement.as<JsonArrayConst>();
            if (argument.size() < 1)
                continue;
            const char *remoteName = argument[0].as<const char *>();
            if (remoteName == nullptr)
                continue;
            bool known = false;
            for (size_t a = 0; a < action.argumentCount() && !known; ++a)
                known = strcmp(remoteName, action.argument(a).name) == 0;
            // A missing "required" means required, as it did before the field existed.
            const bool required = argument.size() >= 3 ? argument[2].as<bool>() : true;
            if (!known && required)
                LOG_WARNING("RM", "Action '%s/%s' requires argument '%s' this caller does not know",
                            deviceName.c_str(), resource->name_.c_str(), remoteName);
        }
    }
}
#endif // NM_ENABLE_REMOTE_RESOURCE_VERIFICATION

// Validate, then verify, then deliver. This is the manifest the manager itself
// reads: verification compares against the compact form because that is the
// cheap one, and on a real 20-resource manifest it is 375 bytes against 1753.
// The saving is a parse, not just a transfer -- and it lands during the connect
// burst, when retained manifests replay while the TLS session is still holding
// its record buffers, which is the exact moment this device has least to spare.
//
// The handler is promised a payload that is well formed and in an encoding this
// build understands, so it never has to re-check either. An empty payload is a
// retained withdrawal and is passed straight through, matching how the JSON
// manifest reports the same thing.
void ResourcesManager::applyEncodedManifest(const String &deviceName, const String &message)
{
    if (message.length() > MaxManifestLength)
    {
        LOG_WARNING("RM", "Ignored oversized encoded manifest from '%s' (%u bytes)",
                    deviceName.c_str(), (unsigned)message.length());
        return;
    }
    // A withdrawal removes declarations and nothing else: values keep whatever
    // freshness their own /state gave them, so there is nothing to verify.
    if (message.length() == 0)
    {
        if (encodedManifestHandler_ != nullptr)
            encodedManifestHandler_(deviceName, message);
        return;
    }

    JsonDocument probe;
    if (deserializeMsgPack(probe, message.c_str(), message.length()) || !probe.is<JsonArray>())
    {
        LOG_WARNING("RM", "Ignored malformed encoded manifest from '%s'", deviceName.c_str());
        return;
    }
    JsonArrayConst root = probe.as<JsonArrayConst>();
    const uint8_t encoding = root.size() != 0 ? root[0].as<uint8_t>() : 0;
    if (root.size() < 3 || encoding != ManifestEncodingVersion)
    {
        // Not an error: a device newer than this one. It publishes the JSON
        // manifest too, which is the whole point of still publishing it.
        LOG_WARNING("RM", "Encoded manifest from '%s' is encoding %u, this build reads %u -- "
                          "use the JSON manifest instead",
                    deviceName.c_str(), (unsigned)encoding, (unsigned)ManifestEncodingVersion);
        return;
    }

#if NM_ENABLE_REMOTE_RESOURCE_VERIFICATION
    verifyAgainstManifest(deviceName, root[2].as<JsonArrayConst>());
#endif

    // Independently of verification: a handler asked for every device's
    // manifest, not for this manager's opinion of it.
    if (encodedManifestHandler_ != nullptr)
        encodedManifestHandler_(deviceName, message);
}

bool ResourcesManager::handleIngressMessage(const String &topic, const String &message)
{
    const int firstSlash = topic.indexOf('/');
    if (firstSlash <= 0)
        return false;
    const String deviceName = topic.substring(0, firstSlash);
    if (!DeviceIdentity::validDeviceName(deviceName))
        return false;
    const String path = topic.substring(firstSlash + 1);
    // Manifests are a sibling subtree of resource, not a member of it, so the
    // two are told apart by prefix and never by counting segments.
    if (path == "manifest")
    {
        // Consumed only when a handler wants every device's manifest. The
        // manager's own verification reads the compact form now, so nothing
        // inside the library needs this topic; without a handler it falls
        // through to the project callback rather than being parsed here.
        if (manifestHandler_ == nullptr || deviceName == gDeviceIdentity.getDeviceName())
            return false;
        applyOtherDeviceManifest(deviceName, message);
        return true;
    }
    if (path == "manifest/msgpack")
    {
        // Consumed when verification wants it for a resource bound from that
        // device, or when a handler wants every manifest; whether the payload
        // then turns out to be valid does not change that. This device's own
        // manifest is not other-device traffic and is left alone.
        const bool wantedForVerification =
            verifiesRemoteManifests() && remoteOwnerInUse(deviceName, nullptr);
        if (deviceName == gDeviceIdentity.getDeviceName() ||
            (!wantedForVerification && encodedManifestHandler_ == nullptr))
            return false;
        applyEncodedManifest(deviceName, message);
        return true;
    }
    if (!path.startsWith("resource/"))
        return false;

    const String tail = path.substring(sizeof("resource/") - 1);
    const int finalSlash = tail.indexOf('/');
    if (finalSlash <= 0)
        return false;
    const String name = tail.substring(0, finalSlash);
    const String operation = tail.substring(finalSlash + 1);
    if (!validSegment(name) || operation.indexOf('/') >= 0)
        return false;

    NetResource *resource = findResource(deviceName, name);
    if (resource == nullptr)
        return false;

    // From here the topic addresses a known resource with an operation that
    // fits its role, so the message is consumed whatever happens next.
    // Mismatches (this device's own /state echo, say) are left alone.
    if (operation == "state" && resource->kind_ == NetResourceType::VALUE && !resource->isOwned())
    {
        applyRemoteState(*static_cast<NetValueResource *>(resource), message);
        return true;
    }
    if (operation == "set" && resource->kind_ == NetResourceType::VALUE && resource->isOwned())
    {
        applyManagedWrite(*static_cast<NetValueResource *>(resource), message);
        return true;
    }
    if (operation == "invoke" && resource->kind_ == NetResourceType::ACTION && resource->isOwned())
    {
        // Raw MQTT has nowhere to put a result; correlated callers use
        // executeAction() directly and keep it.
        const ActionResult result =
            executeAction(*static_cast<NetActionResource *>(resource), message);
        if (!result.success)
            LOG_WARNING("RM", "Action '%s' failed: %s", name.c_str(), result.result.c_str());
        return true;
    }
    return false;
}

#endif // NM_ENABLE_RESOURCES
