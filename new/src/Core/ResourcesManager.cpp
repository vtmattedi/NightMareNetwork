#include "ResourcesManager.h"
#include "DeviceIdentity.h"

#include <ArduinoJson.h>

namespace
{
constexpr size_t MaxSegmentLength = 64;
constexpr size_t MaxValueLength = NetResourceMaxPayloadLength;
constexpr size_t MaxManifestLength = 16384;
constexpr char ResourcesPath[] = "/resources";

const char *kindName(NetResourceKind kind)
{
    return kind == NetResourceKind::VALUE ? "value" : "action";
}

const char *accessName(NetAccess access)
{
    return access == NetAccess::READ_WRITE ? "read_write" : "read";
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

void ResourcesManager::setPublisher(ResourcePublisher *publisher)
{
    publisher_ = publisher;
    if (publisher_ != nullptr)
        announceAll();
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

bool ResourcesManager::bindResource(NetResource *resource)
{
    if (resource == nullptr || resource->resourceManager != nullptr ||
        resourceCount_ >= MaxResources || !validSegment(resource->name))
        return false;

    const String &thisDevice = gDeviceIdentity.getDeviceName();
    String ownerName = resource->ownerDevice.deviceName;
    if (resource->authority == ResourceAuthority::THIS_DEVICE)
    {
        if (!validSegment(thisDevice) || (ownerName.length() != 0 && ownerName != thisDevice))
            return false;
        ownerName = thisDevice;
    }
    else if (!validSegment(ownerName) || ownerName == thisDevice || ownerName == "all")
    {
        return false;
    }
    if (findResource(ownerName, resource->name) != nullptr)
        return false;

    resource->ownerDevice.deviceName = ownerName;
    gDeviceIdentity.lockAddress();
    resource->resourceManager = this;
    resources_[resourceCount_++] = resource;
    if (resource->authority == ResourceAuthority::THIS_DEVICE)
    {
        if (resource->kind == NetResourceKind::ACTION)
            static_cast<NetActionResource *>(resource)->freshness = ResourceFreshness::FRESH;
        publishManifest();
        if (resource->kind == NetResourceKind::VALUE)
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

        const bool owned = resource->authority == ResourceAuthority::THIS_DEVICE;
        if (owned && resource->kind == NetResourceKind::VALUE && publisher_ != nullptr)
            publisher_->publish(topicFor(*resource, "state"), String(), true);

        for (int j = i; j < resourceCount_ - 1; ++j)
            resources_[j] = resources_[j + 1];
        resources_[--resourceCount_] = nullptr;
        resource->resourceManager = nullptr;
        if (owned)
            publishManifest();
        return;
    }
}

String ResourcesManager::topicFor(const NetResource &resource, const char *suffix) const
{
    String topic = resource.ownerDevice.deviceName + ResourcesPath + "/" + resource.name;
    if (suffix != nullptr && suffix[0] != '\0')
    {
        topic += '/';
        topic += suffix;
    }
    return topic;
}

bool ResourcesManager::publishManifest()
{
    const String &thisDevice = gDeviceIdentity.getDeviceName();
    if (publisher_ == nullptr || !validSegment(thisDevice))
        return false;

    DynamicJsonDocument doc(MaxManifestLength);
    doc["version"] = 1;
    JsonArray items = doc.createNestedArray("resources");
    for (int i = 0; i < resourceCount_; ++i)
    {
        const NetResource &resource = *resources_[i];
        if (resource.authority != ResourceAuthority::THIS_DEVICE)
            continue;
        JsonObject item = items.createNestedObject();
        item["name"] = resource.name;
        item["kind"] = kindName(resource.kind);
        item["access"] = accessName(resource.access);
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
    if (publisher_ == nullptr || resource.authority != ResourceAuthority::THIS_DEVICE ||
        resource.freshness != ResourceFreshness::FRESH)
        return false;

    DynamicJsonDocument doc(resource.value.length() + 128);
    doc["value"] = resource.value;
    if (doc.overflowed())
        return false;
    String payload;
    if (serializeJson(doc, payload) == 0)
        return false;
    return publisher_->publish(topicFor(resource, "state"), payload, true);
}

bool ResourcesManager::announceAll()
{
    if (!publishManifest())
        return false;
    bool published = true;
    for (int i = 0; i < resourceCount_; ++i)
    {
        NetResource *resource = resources_[i];
        if (resource->authority == ResourceAuthority::THIS_DEVICE && resource->kind == NetResourceKind::VALUE)
        {
            NetValueResource &value = *static_cast<NetValueResource *>(resource);
            if (value.freshness == ResourceFreshness::FRESH && !publishState(value))
                published = false;
        }
    }
    return published;
}

bool ResourcesManager::setValue(NetValueResource &resource, const String &newValue)
{
    if (resource.resourceManager != this || newValue.length() > MaxValueLength)
        return false;
    if (resource.authority == ResourceAuthority::OTHER_DEVICE)
    {
        if (resource.access != NetAccess::READ_WRITE || publisher_ == nullptr)
            return false;
        return publisher_->publish(topicFor(resource, "set"), newValue, false);
    }

    resource.value = newValue;
    resource.freshness = ResourceFreshness::FRESH;
    publishState(resource); // An owned update remains valid if the transport is offline.
    return true;
}

bool ResourcesManager::invoke(NetActionResource &resource, const String &payload)
{
    if (resource.resourceManager != this || resource.access != NetAccess::READ_WRITE ||
        payload.length() > MaxValueLength)
        return false;
    if (resource.authority == ResourceAuthority::OTHER_DEVICE)
        return publisher_ != nullptr && publisher_->publish(topicFor(resource, "invoke"), payload, false);

    NetActionResource::InvokeHandler handler = resource.onInvoke != nullptr
                                                   ? resource.onInvoke : actionHandler_;
    return handler != nullptr && handler(resource, payload);
}

bool ResourcesManager::applyOtherDeviceManifest(const String &deviceName, const String &message)
{
    if (deviceName == gDeviceIdentity.getDeviceName() || message.length() > MaxManifestLength)
        return false;
    if (message.length() == 0)
    {
        bool matched = false;
        for (int i = 0; i < resourceCount_; ++i)
        {
            NetResource *resource = resources_[i];
            if (resource->authority != ResourceAuthority::OTHER_DEVICE ||
                resource->ownerDevice.deviceName != deviceName)
                continue;
            matched = true;
            if (resource->kind == NetResourceKind::ACTION)
                static_cast<NetActionResource *>(resource)->freshness = ResourceFreshness::STALE;
            else
                static_cast<NetValueResource *>(resource)->freshness = ResourceFreshness::STALE;
        }
        return matched;
    }
    DynamicJsonDocument doc(MaxManifestLength);
    if (deserializeJson(doc, message) || !doc["resources"].is<JsonArray>())
        return false;

    bool matched = false;
    JsonArray items = doc["resources"].as<JsonArray>();
    for (int i = 0; i < resourceCount_; ++i)
    {
        NetResource *resource = resources_[i];
        if (resource->authority != ResourceAuthority::OTHER_DEVICE || resource->ownerDevice.deviceName != deviceName)
            continue;
        matched = true;
        bool found = false;
        for (JsonObject item : items)
        {
            if (item["name"].as<String>() != resource->name ||
                item["kind"].as<String>() != kindName(resource->kind))
                continue;
            const String access = item["access"].as<String>();
            if (access == "read")
                resource->access = NetAccess::READ;
            else if (access == "read_write")
                resource->access = NetAccess::READ_WRITE;
            else
                continue;
            found = true;
            break;
        }
        if (resource->kind == NetResourceKind::ACTION)
            static_cast<NetActionResource *>(resource)->freshness =
                found ? ResourceFreshness::FRESH : ResourceFreshness::STALE;
        else if (!found)
            static_cast<NetValueResource *>(resource)->freshness = ResourceFreshness::STALE;
    }
    return matched;
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

    if (operation == "state" && resource->kind == NetResourceKind::VALUE &&
        resource->authority == ResourceAuthority::OTHER_DEVICE)
    {
        if (message.length() == 0)
        {
            static_cast<NetValueResource *>(resource)->freshness = ResourceFreshness::STALE;
            return true;
        }
        if (message.length() > MaxValueLength + 128)
            return false;
        DynamicJsonDocument doc(message.length() + 128);
        if (deserializeJson(doc, message) || !doc["value"].is<const char *>())
            return false;
        const String incomingValue = doc["value"].as<String>();
        if (incomingValue.length() > MaxValueLength)
            return false;
        NetValueResource &value = *static_cast<NetValueResource *>(resource);
        value.value = incomingValue;
        value.freshness = ResourceFreshness::FRESH;
        return true;
    }

    if (operation == "set" && resource->kind == NetResourceKind::VALUE &&
        resource->authority == ResourceAuthority::THIS_DEVICE &&
        resource->access == NetAccess::READ_WRITE)
    {
        NetValueResource &value = *static_cast<NetValueResource *>(resource);
        NetValueResource::WriteHandler handler = value.onWrite != nullptr
                                                    ? value.onWrite : valueHandler_;
        return handler != nullptr && message.length() <= MaxValueLength &&
               handler(value, message) && setValue(value, message);
    }

    if (operation == "invoke" && resource->kind == NetResourceKind::ACTION &&
        resource->authority == ResourceAuthority::THIS_DEVICE)
        return invoke(*static_cast<NetActionResource *>(resource), message);

    return false;
}
