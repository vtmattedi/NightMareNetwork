#include <NightMare/Features.h>
#if NM_ENABLE_RESOURCES
#include "ResourcesManager.h"
#include "DeviceIdentity.h"

#include <ArduinoJson.h>

namespace
{
constexpr size_t MaxSegmentLength = 64;
constexpr size_t MaxValueLength = NetResourceMaxPayloadLength;
constexpr size_t MaxManifestLength = 16384;
constexpr char ResourcesPath[] = "/resources";
constexpr int ManifestVersion = 2;

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

// JSON is the canonical machine payload for action arguments. Positional human
// syntax is a command-layer concern and never reaches this file.
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

bool schemaHasArgument(const NetActionResource &action, const char *name)
{
    for (size_t i = 0; i < action.argumentCount(); ++i)
        if (strcmp(action.argument(i).name, name) == 0)
            return true;
    return false;
}
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

bool ResourcesManager::hasResolvedSource(const NetResource &resource)
{
    return validSegment(resource.ownerDevice.deviceName) && validSegment(resource.name);
}

bool ResourcesManager::remoteOwnerInUse(const String &deviceName, const NetResource *exclude) const
{
    for (int i = 0; i < resourceCount_; ++i)
    {
        const NetResource *resource = resources_[i];
        if (resource == exclude || resource->isOwned())
            continue;
        if (hasResolvedSource(*resource) && resource->ownerDevice.deviceName == deviceName)
            return true;
    }
    return false;
}

String ResourcesManager::topicFor(const String &deviceName, const String &resourceName,
                                  const char *suffix) const
{
    String topic = deviceName + ResourcesPath + "/" + resourceName;
    if (suffix != nullptr && suffix[0] != '\0')
    {
        topic += '/';
        topic += suffix;
    }
    return topic;
}

String ResourcesManager::topicFor(const NetResource &resource, const char *suffix) const
{
    return topicFor(resource.ownerDevice.deviceName, resource.name, suffix);
}

String ResourcesManager::manifestTopicFor(const String &deviceName) const
{
    return deviceName + ResourcesPath;
}

String ResourcesManager::ingressTopicFor(const NetResource &resource, const String &deviceName,
                                         const String &resourceName) const
{
    if (!validSegment(deviceName) || !validSegment(resourceName))
        return String();

    if (resource.kind == NetResourceType::VALUE)
    {
        // Remote values listen to their owner. Managed values only listen when
        // somebody else is allowed to write them.
        if (!resource.isOwned())
            return topicFor(deviceName, resourceName, "state");
        const NetValueResource &value = static_cast<const NetValueResource &>(resource);
        if (value.access == AccessPolicy::READ_WRITE)
            return topicFor(deviceName, resourceName, "set");
        return String();
    }

    // Only the implementing device listens for invocations.
    if (resource.isOwned())
        return topicFor(deviceName, resourceName, "invoke");
    return String();
}

String ResourcesManager::ingressTopicFor(const NetResource &resource) const
{
    return ingressTopicFor(resource, resource.ownerDevice.deviceName, resource.name);
}

void ResourcesManager::subscribeResource(const NetResource &resource, bool includeManifest)
{
    if (subscriber_ == nullptr)
        return;
    if (includeManifest && !resource.isOwned() && hasResolvedSource(resource))
        subscriber_->subscribe(manifestTopicFor(resource.ownerDevice.deviceName));
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
    if (removeManifest && !resource.isOwned() && hasResolvedSource(resource))
        subscriber_->unsubscribe(manifestTopicFor(resource.ownerDevice.deviceName));
}

void ResourcesManager::subscribeAll()
{
    for (int i = 0; i < resourceCount_; ++i)
    {
        const NetResource &resource = *resources_[i];
        // One manifest subscription per remote device: claim it for the first
        // resource that names that owner.
        bool includeManifest = !resource.isOwned() && hasResolvedSource(resource);
        for (int j = 0; includeManifest && j < i; ++j)
            if (!resources_[j]->isOwned() && hasResolvedSource(*resources_[j]) &&
                resources_[j]->ownerDevice.deviceName == resource.ownerDevice.deviceName)
                includeManifest = false;
        subscribeResource(resource, includeManifest);
    }
}

bool ResourcesManager::needsSubscription(const String &topicFilter) const
{
    for (int i = 0; i < resourceCount_; ++i)
    {
        const NetResource &resource = *resources_[i];
        if (ingressTopicFor(resource) == topicFilter && topicFilter.length() != 0)
            return true;
        if (!resource.isOwned() && hasResolvedSource(resource) &&
            manifestTopicFor(resource.ownerDevice.deviceName) == topicFilter)
            return true;
    }
    return false;
}

NetResource *ResourcesManager::findResource(const String &deviceName, const String &name) const
{
    for (int i = 0; i < resourceCount_; ++i)
    {
        NetResource *resource = resources_[i];
        if (resource->ownerDevice.deviceName == deviceName && resource->name == name)
            return resource;
    }
    return nullptr;
}

// Unlike findResource(), this cannot be fooled by `self` appearing first.
bool ResourcesManager::addressTakenByOther(const String &deviceName, const String &name,
                                           const NetResource *self) const
{
    for (int i = 0; i < resourceCount_; ++i)
    {
        const NetResource *resource = resources_[i];
        if (resource != self && resource->ownerDevice.deviceName == deviceName &&
            resource->name == name)
            return true;
    }
    return false;
}

bool ResourcesManager::bindResource(NetResource *resource)
{
    if (resource == nullptr || resource->resourceManager != nullptr ||
        resourceCount_ >= MaxResources)
        return false;
    if (!validSegment(resource->name) && !(!resource->isOwned() && resource->name.length() == 0))
    {
        LOG_ERROR("RM", "Cannot bind resource '%s': invalid name", resource->name.c_str());
        return false;
    }
    if (resource->kind == NetResourceType::ACTION &&
        !validActionSchema(*static_cast<NetActionResource *>(resource)))
    {
        LOG_ERROR("RM", "Cannot bind action '%s': invalid argument schema", resource->name.c_str());
        return false;
    }

    if (resource->isOwned())
    {
        // A managed resource is declared without an owner and adopts this
        // device at bind time.
        const String &thisDevice = gDeviceIdentity.getDeviceName();
        if (!validSegment(thisDevice))
        {
            LOG_ERROR("RM", "Cannot bind resource '%s': invalid local device name",
                      resource->name.c_str());
            return false;
        }
        const String &declaredOwner = resource->ownerDevice.deviceName;
        if (declaredOwner.length() != 0 && declaredOwner != thisDevice)
        {
            LOG_ERROR("RM", "Cannot bind managed resource '%s': declared owner '%s' is not this device",
                      resource->name.c_str(), declaredOwner.c_str());
            return false;
        }
        resource->ownerDevice.deviceName = thisDevice;
        gDeviceIdentity.lockAddress();
    }
    else if (hasResolvedSource(*resource) &&
             resource->ownerDevice.deviceName == gDeviceIdentity.getDeviceName())
    {
        LOG_ERROR("RM", "Cannot bind remote resource '%s': it points at this device",
                  resource->name.c_str());
        return false;
    }

    // An unconfigured remote resource has no logical address yet, so it cannot
    // collide with anything.
    if (hasResolvedSource(*resource) &&
        findResource(resource->ownerDevice.deviceName, resource->name) != nullptr)
    {
        LOG_ERROR("RM", "Cannot bind resource '%s' for device '%s': already bound",
                  resource->name.c_str(), resource->ownerDevice.deviceName.c_str());
        return false;
    }

    const bool firstFromOwner = !resource->isOwned() && hasResolvedSource(*resource) &&
                                !remoteOwnerInUse(resource->ownerDevice.deviceName, resource);
    resource->resourceManager = this;
    resources_[resourceCount_++] = resource;
    LOG("RM", "Bound %s '%s' (%s) for device '%s'", kindName(resource->kind),
        resource->name.c_str(), resource->isOwned() ? "managed" : "remote",
        resource->ownerDevice.deviceName.c_str());

    subscribeResource(*resource, firstFromOwner);

    if (resource->isOwned())
    {
        publishManifest();
        if (resource->kind == NetResourceType::VALUE)
            publishState(*static_cast<NetValueResource *>(resource));
    }
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
        if (managed && resource->kind == NetResourceType::VALUE && publisher_ != nullptr &&
            static_cast<NetValueResource *>(resource)->hasAuthoritativeValue() &&
            hasResolvedSource(*resource))
            publisher_->publish(topicFor(*resource, "state"), String(), true);

        for (int j = i; j < resourceCount_ - 1; ++j)
            resources_[j] = resources_[j + 1];
        resources_[--resourceCount_] = nullptr;
        resource->resourceManager = nullptr;

        // Now that it is out of the registry, "last one for this owner" is an
        // honest question to ask.
        unsubscribeResource(*resource,
                            !remoteOwnerInUse(resource->ownerDevice.deviceName, resource));
        if (managed)
            publishManifest();
        return;
    }
}

void ResourcesManager::notifySourceChanged(NetResource &resource, const NetDeviceIdentity &oldOwner,
                                           const String &oldName)
{
    if (resource.resourceManager != this || resource.isOwned())
        return;

    const String &newOwner = resource.ownerDevice.deviceName;
    const bool ownerChanged = oldOwner.deviceName != newOwner;

    if (subscriber_ != nullptr)
    {
        const String oldIngress = ingressTopicFor(resource, oldOwner.deviceName, oldName);
        if (oldIngress.length() != 0)
            subscriber_->unsubscribe(oldIngress);
        // The resource already carries the new owner, so it no longer counts
        // towards the old one.
        if (ownerChanged && validSegment(oldOwner.deviceName) &&
            !remoteOwnerInUse(oldOwner.deviceName, nullptr))
            subscriber_->unsubscribe(manifestTopicFor(oldOwner.deviceName));
    }

    if (!hasResolvedSource(resource))
        return; // Registered, but pointed at nothing subscribable.

    // setSource() cannot fail, so a target that would make routing ambiguous is
    // refused by detaching the resource instead: with no owner it matches no
    // topic, stays registered, and can be pointed somewhere valid later. Left
    // addressable, it could shadow the resource that legitimately holds that
    // address, since ingress routes to the first match.
    const char *refusal = nullptr;
    if (newOwner == gDeviceIdentity.getDeviceName())
        refusal = "it names this device";
    else if (addressTakenByOther(newOwner, resource.name, &resource))
        refusal = "another bound resource already represents it";
    if (refusal != nullptr)
    {
        LOG_ERROR("RM", "Refused source '%s/%s' for remote resource: %s",
                  newOwner.c_str(), resource.name.c_str(), refusal);
        resource.ownerDevice.deviceName = String();
        return;
    }

    if (subscriber_ != nullptr)
    {
        if (ownerChanged && !remoteOwnerInUse(newOwner, &resource))
            subscriber_->subscribe(manifestTopicFor(newOwner));
        const String ingress = ingressTopicFor(resource);
        if (ingress.length() != 0)
            subscriber_->subscribe(ingress);
    }
}

bool ResourcesManager::publishManifest()
{
    const String &thisDevice = gDeviceIdentity.getDeviceName();
    if (publisher_ == nullptr || !validSegment(thisDevice))
        return false;

    DynamicJsonDocument doc(MaxManifestLength);
    doc["version"] = ManifestVersion;
    JsonArray items = doc.createNestedArray("resources");
    for (int i = 0; i < resourceCount_; ++i)
    {
        const NetResource &resource = *resources_[i];
        if (!resource.isOwned())
            continue; // Only what this device implements.
        JsonObject item = items.createNestedObject();
        item["name"] = resource.name;
        item["kind"] = kindName(resource.kind);
        if (resource.kind == NetResourceType::VALUE)
        {
            const NetValueResource &value = static_cast<const NetValueResource &>(resource);
            item["access"] = accessName(value.access);
            item["type"] = valueTypeName(value.valueType);
        }
        else
        {
            const NetActionResource &action = static_cast<const NetActionResource &>(resource);
            JsonArray args = item.createNestedArray("arguments");
            for (size_t a = 0; a < action.argumentCount(); ++a)
            {
                JsonObject argument = args.createNestedObject();
                argument["name"] = action.argument(a).name;
                argument["type"] = valueTypeName(action.argument(a).type);
            }
        }
    }
    if (doc.overflowed())
        return false;

    String payload;
    if (serializeJson(doc, payload) == 0)
        return false;
    return publisher_->publish(thisDevice + ResourcesPath, payload, true);
}

bool ResourcesManager::publishState(const NetValueResource &resource)
{
    if (publisher_ == nullptr || !resource.isOwned() || !resource.hasAuthoritativeValue() ||
        !hasResolvedSource(resource))
        return false;
    // The wire format is the codec's own representation: "23.5", "true", raw
    // string. The declared type is already in the manifest.
    return publisher_->publish(topicFor(resource, "state"), resource.encodedValue(), true);
}

bool ResourcesManager::announceAll()
{
    if (!publishManifest())
        return false;
    bool published = true;
    for (int i = 0; i < resourceCount_; ++i)
    {
        NetResource *resource = resources_[i];
        if (!resource->isOwned() || resource->kind != NetResourceType::VALUE)
            continue;
        NetValueResource &value = *static_cast<NetValueResource *>(resource);
        if (value.hasAuthoritativeValue() && !publishState(value))
            published = false;
    }
    return published;
}

bool ResourcesManager::setValue(NetValueResource &resource, const String &encoded)
{
    if (resource.resourceManager != this || encoded.length() > MaxValueLength)
        return false;

    if (!resource.isOwned())
    {
        // A remote write only counts once the transport took it; otherwise the
        // caller would start an optimistic window over a request nobody sent.
        if (resource.access != AccessPolicy::READ_WRITE || publisher_ == nullptr ||
            !hasResolvedSource(resource))
            return false;
        return publisher_->publish(topicFor(resource, "set"), encoded, false);
    }

    // Local truth does not depend on the network: publish best-effort and let
    // the caller commit regardless. announceAll() retries after a reconnect.
    if (publisher_ != nullptr && hasResolvedSource(resource))
        publisher_->publish(topicFor(resource, "state"), encoded, true);
    return true;
}

bool ResourcesManager::invoke(NetActionResource &resource, const String &payload)
{
    if (resource.resourceManager != this || payload.length() > MaxValueLength)
        return false;
    // Only a remote action is dispatched; a managed one is executed instead.
    if (resource.isOwned() || !hasResolvedSource(resource) || publisher_ == nullptr)
        return false;
    return publisher_->publish(topicFor(resource, "invoke"), payload, false);
}

ActionResult ResourcesManager::executeAction(NetActionResource &action, const String &canonicalPayload)
{
    if (!action.isOwned())
        return {false, String("not implemented by this device")};
    if (canonicalPayload.length() > MaxValueLength)
        return {false, String("payload too large")};

    if (action.argumentCount() == 0)
    {
        const String trimmed = canonicalPayload;
        if (trimmed.length() != 0 && trimmed != "{}")
            return {false, String("action takes no arguments")};
        return action.execute(canonicalPayload);
    }

    DynamicJsonDocument doc(canonicalPayload.length() + 256);
    if (deserializeJson(doc, canonicalPayload) || !doc.is<JsonObject>())
        return {false, String("payload is not a JSON object")};
    JsonObjectConst arguments = doc.as<JsonObjectConst>();

    for (size_t i = 0; i < action.argumentCount(); ++i)
    {
        const ActionArgMetadata &meta = action.argument(i);
        JsonVariantConst value = arguments[meta.name];
        if (value.isNull())
            return {false, String("missing argument '") + meta.name + "'"};
        if (!argumentTypeMatches(meta.type, value))
            return {false, String("argument '") + meta.name + "' has the wrong type"};
    }
    for (JsonPairConst entry : arguments)
    {
        if (!schemaHasArgument(action, entry.key().c_str()))
            return {false, String("unknown argument '") + entry.key().c_str() + "'"};
    }

    return action.execute(canonicalPayload);
}

bool ResourcesManager::applyOtherDeviceManifest(const String &deviceName, const String &message)
{
    if (deviceName == gDeviceIdentity.getDeviceName() || message.length() > MaxManifestLength)
        return false;

    // An empty retained manifest means the device withdrew its declarations.
    if (message.length() == 0)
    {
        bool matched = false;
        for (int i = 0; i < resourceCount_; ++i)
        {
            NetResource *resource = resources_[i];
            if (resource->isOwned() || resource->ownerDevice.deviceName != deviceName)
                continue;
            matched = true;
            if (resource->kind == NetResourceType::VALUE)
                static_cast<NetValueResource *>(resource)->freshness = ResourceFreshness::STALE;
        }
        if (manifestHandler_ != nullptr)
            manifestHandler_(deviceName, message);
        return matched || manifestHandler_ != nullptr;
    }

    DynamicJsonDocument doc(MaxManifestLength);
    if (deserializeJson(doc, message) || !doc["resources"].is<JsonArray>())
        return false;

    bool matched = false;
    JsonArray items = doc["resources"].as<JsonArray>();
    for (int i = 0; i < resourceCount_; ++i)
    {
        NetResource *resource = resources_[i];
        if (resource->isOwned() || resource->ownerDevice.deviceName != deviceName)
            continue;
        matched = true;

        JsonObject declaration;
        for (JsonObject item : items)
        {
            if (item["name"].as<String>() == resource->name)
            {
                declaration = item;
                break;
            }
        }

        // The local declaration is intentional and is never rewritten from a
        // remote manifest; a disagreement marks the source unusable instead.
        bool compatible = !declaration.isNull() &&
                          declaration["kind"].as<String>() == kindName(resource->kind);
        if (compatible && resource->kind == NetResourceType::VALUE)
        {
            const NetValueResource &value = *static_cast<NetValueResource *>(resource);
            if (declaration["type"].as<String>() != valueTypeName(value.valueType))
                compatible = false;
            else if (value.access == AccessPolicy::READ_WRITE &&
                     declaration["access"].as<String>() != accessName(AccessPolicy::READ_WRITE))
                compatible = false;
        }

        if (!compatible)
        {
            LOG_WARNING("RM", "Source '%s/%s' is missing or incompatible with the local declaration",
                        deviceName.c_str(), resource->name.c_str());
            if (resource->kind == NetResourceType::VALUE)
                static_cast<NetValueResource *>(resource)->freshness = ResourceFreshness::STALE;
        }
    }
    if (manifestHandler_ != nullptr)
        manifestHandler_(deviceName, message);
    return matched || manifestHandler_ != nullptr;
}

bool ResourcesManager::handleIngressMessage(const String &topic, const String &message)
{
    const int firstSlash = topic.indexOf('/');
    if (firstSlash <= 0)
        return false;
    const String deviceName = topic.substring(0, firstSlash);
    if (!validSegment(deviceName))
        return false;
    const String path = topic.substring(firstSlash + 1);
    if (path == "resources")
        return applyOtherDeviceManifest(deviceName, message);
    if (!path.startsWith("resources/"))
        return false;

    const String tail = path.substring(sizeof("resources/") - 1);
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

    // Owner state for a value this device only observes.
    if (operation == "state" && resource->kind == NetResourceType::VALUE && !resource->isOwned())
    {
        NetValueResource &value = *static_cast<NetValueResource *>(resource);
        if (message.length() == 0)
        {
            // The retained state was deleted. The last known value stays
            // readable; only setSource() discards it, because only that changes
            // what the resource represents.
            value.freshness = ResourceFreshness::STALE;
            return true;
        }
        if (message.length() > MaxValueLength)
            return false;
        // The decode belongs to NetValue<T>/NetCodec<T>, never here.
        return value.applyEncodedOwnerValue(message);
    }

    // A write request aimed at a value this device implements.
    if (operation == "set" && resource->kind == NetResourceType::VALUE && resource->isOwned())
    {
        NetValueResource &value = *static_cast<NetValueResource *>(resource);
        if (value.access != AccessPolicy::READ_WRITE || message.length() > MaxValueLength)
            return false;
        if (!value.applyEncodedWrite(message))
            return false; // Rejected or undecodable: state is unchanged, so say nothing.
        publishState(value);
        return true;
    }

    // An invocation of an action this device implements.
    if (operation == "invoke" && resource->kind == NetResourceType::ACTION && resource->isOwned())
    {
        // Raw MQTT has nowhere to put a result; correlated callers use
        // executeAction() directly and keep it.
        const ActionResult result =
            executeAction(*static_cast<NetActionResource *>(resource), message);
        if (!result.success)
            LOG_WARNING("RM", "Action '%s' failed: %s", name.c_str(), result.result.c_str());
        return result.success;
    }

    return false;
}

#endif // NM_ENABLE_RESOURCES
