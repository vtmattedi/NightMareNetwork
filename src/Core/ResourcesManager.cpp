#include <NightMare/Features.h>
#if NM_ENABLE_RESOURCES
#include "ResourcesManager.h"
#include "DeviceIdentity.h"

#include <ArduinoJson.h>

namespace
{
constexpr size_t MaxSegmentLength = 64;
constexpr size_t MaxValueLength = NetResourceMaxPayloadLength;
constexpr size_t MaxManifestLength = NetResourceMaxManifestLength;
constexpr int ManifestVersion = 2;

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
    DynamicJsonDocument doc(payload.length() * 2 + 256);
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

void ResourcesManager::subscribeResource(const NetResource &resource, bool includeManifest)
{
    if (subscriber_ == nullptr)
        return;
    if (includeManifest && !resource.isOwned() && hasResolvedSource(resource))
        subscriber_->subscribe(resolveResourceManifestTopic(resource.ownerDevice_.deviceName));
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
        subscriber_->unsubscribe(resolveResourceManifestTopic(resource.ownerDevice_.deviceName));
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
                resources_[j]->ownerDevice_.deviceName == resource.ownerDevice_.deviceName)
                includeManifest = false;
        subscribeResource(resource, includeManifest);
    }
}

bool ResourcesManager::needsSubscription(const String &topicFilter) const
{
    if (topicFilter.length() == 0)
        return false;
    for (int i = 0; i < resourceCount_; ++i)
    {
        const NetResource &resource = *resources_[i];
        if (ingressTopicFor(resource) == topicFilter)
            return true;
        if (!resource.isOwned() && hasResolvedSource(resource) &&
            resolveResourceManifestTopic(resource.ownerDevice_.deviceName) == topicFilter)
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
            subscriber_->unsubscribe(resolveResourceManifestTopic(oldOwner.deviceName));
    }

    if (!sourceConfigured(resource))
        return; // Registered, but not pointed at anything yet.

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
        return;
    }

    if (subscriber_ != nullptr)
    {
        if (ownerChanged && !remoteOwnerInUse(newOwner, &resource))
            subscriber_->subscribe(resolveResourceManifestTopic(newOwner));
        const String ingress = ingressTopicFor(resource);
        if (ingress.length() != 0)
            subscriber_->subscribe(ingress);
    }
}

bool ResourcesManager::publishManifest()
{
    const String &thisDevice = gDeviceIdentity.getDeviceName();
    if (publisher_ == nullptr || !DeviceIdentity::validDeviceName(thisDevice))
        return false;

    String payload;
    if (!serializeManifest(payload))
        return false;
    return publisher_->publish(resolveResourceManifestTopic(thisDevice), payload, true);
}

bool ResourcesManager::serializeManifest(String &payload) const
{
    DynamicJsonDocument doc(MaxManifestLength);
    doc["version"] = ManifestVersion;
    JsonArray items = doc.createNestedArray("resources");
    for (int i = 0; i < resourceCount_; ++i)
    {
        const NetResource &resource = *resources_[i];
        if (!resource.isOwned())
            continue; // Only what this device implements.
        JsonObject item = items.createNestedObject();
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
            JsonArray args = item.createNestedArray("arguments");
            for (size_t a = 0; a < action.argumentCount(); ++a)
            {
                JsonObject argument = args.createNestedObject();
                argument["name"] = action.argument(a).name;
                argument["type"] = valueTypeName(action.argument(a).type);
                argument["required"] = action.argument(a).required;
            }
        }
    }
    if (doc.overflowed())
        return false;

    payload = String();
    if (serializeJson(doc, payload) == 0)
        return false;
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
    auto displayName = [](const NetResource &resource) -> String {
        return resource.name_.length() == 0 ? String("<unnamed>") : resource.name_;
    };
    auto shortNameIsAmbiguous = [this](const NetResource &candidate) -> bool {
        if (candidate.name_.length() == 0)
            return false;
        int matches = 0;
        for (int i = 0; i < resourceCount_; ++i)
            if (resources_[i]->name_ == candidate.name_ && ++matches > 1)
                return true;
        return false;
    };
    auto displayValue = [&shortNameIsAmbiguous](const NetResource &resource) -> String {
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
    auto appendColumn = [&response](const String &text, size_t width) {
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
    if (!publishManifest())
        return false;
    bool published = true;
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
    if (!publisher_->publish(resolveResourceManifestTopic(oldDeviceName), String(), true))
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
            String arguments = command.payload;
            arguments.trim();
            if (arguments.length() != 0)
                return {false, String("Usage: >manifest")};

            String manifest;
            if (!serializeManifest(manifest))
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

    auto invokeAction = [this](NetActionResource &action, const String &payload) -> ActionResult {
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

    DynamicJsonDocument doc(MaxManifestLength);
    if (deserializeJson(doc, message) || !doc["resources"].is<JsonArray>())
    {
        // Not valid data, so the handler, which is promised only valid data, is not called.
        LOG_WARNING("RM", "Ignored malformed manifest from '%s'", deviceName.c_str());
        return;
    }

    JsonArray items = doc["resources"].as<JsonArray>();
    for (int i = 0; i < resourceCount_; ++i)
    {
        NetResource *resource = resources_[i];
        if (resource->isOwned() || resource->ownerDevice_.deviceName != deviceName)
            continue;

        JsonObject declaration;
        for (JsonObject item : items)
        {
            if (item["name"].as<String>() == resource->name_)
            {
                declaration = item;
                break;
            }
        }

        // The local declaration is intentional and is never rewritten from a
        // remote manifest. A disagreement is reported, and that is all.
        bool compatible = !declaration.isNull() &&
                          declaration["kind"].as<String>() == kindName(resource->kind_);
        if (resource->kind_ == NetResourceType::VALUE)
        {
            const NetValueResource &value = *static_cast<NetValueResource *>(resource);
            if (compatible && declaration["type"].as<String>() != valueTypeName(value.valueType_))
                compatible = false;
            else if (compatible && value.access_ == AccessPolicy::READ_WRITE &&
                     declaration["access"].as<String>() != accessName(AccessPolicy::READ_WRITE))
                compatible = false;
            if (!compatible)
                LOG_WARNING("RM", "Source '%s/%s' is missing or incompatible with the local declaration",
                            deviceName.c_str(), resource->name_.c_str());
            continue;
        }

        // For an action the manifest is description, never a gate: invoke()
        // keeps working however this comparison turns out.
        if (!compatible)
        {
            LOG_WARNING("RM", "Action '%s/%s' is not declared by its device", deviceName.c_str(),
                        resource->name_.c_str());
            continue;
        }
        const NetActionResource &action = *static_cast<NetActionResource *>(resource);
        if (!action.hasSchema())
            continue;
        JsonArray remoteArgs = declaration["arguments"].as<JsonArray>();
        for (size_t a = 0; a < action.argumentCount(); ++a)
        {
            const ActionArgMetadata &expected = action.argument(a);
            bool found = false;
            for (JsonObject remoteArg : remoteArgs)
            {
                if (remoteArg["name"].as<String>() != expected.name)
                    continue;
                found = true;
                if (remoteArg["type"].as<String>() != valueTypeName(expected.type))
                    LOG_WARNING("RM", "Action '%s/%s' argument '%s' is %s remotely, expected %s",
                                deviceName.c_str(), resource->name_.c_str(), expected.name,
                                remoteArg["type"].as<String>().c_str(), valueTypeName(expected.type));
                break;
            }
            if (!found)
                LOG_WARNING("RM", "Action '%s/%s' does not declare expected argument '%s'",
                            deviceName.c_str(), resource->name_.c_str(), expected.name);
        }
        for (JsonObject remoteArg : remoteArgs)
        {
            const String remoteName = remoteArg["name"].as<String>();
            bool known = false;
            for (size_t a = 0; a < action.argumentCount() && !known; ++a)
                known = remoteName == action.argument(a).name;
            // "required" absent means required, as it did before the field existed.
            const bool required = remoteArg["required"] | true;
            if (!known && required)
                LOG_WARNING("RM", "Action '%s/%s' requires argument '%s' this caller does not know",
                            deviceName.c_str(), resource->name_.c_str(), remoteName.c_str());
        }
    }
    if (manifestHandler_ != nullptr)
        manifestHandler_(deviceName, message);
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
    if (path == "resources")
    {
        // Consumed when some remote resource points at that device, or a
        // discovery handler wants every manifest; whether the payload then turns
        // out to be valid does not change that. This device's own manifest is
        // not other-device traffic and is left alone.
        if (deviceName == gDeviceIdentity.getDeviceName() ||
            (!remoteOwnerInUse(deviceName, nullptr) && manifestHandler_ == nullptr))
            return false;
        applyOtherDeviceManifest(deviceName, message);
        return true;
    }
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
