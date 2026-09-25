#include "NetResources.h"
#include <NightMare/Features.h>
#if NM_ENABLE_RESOURCES
#include "ResourcesManager.h"
#endif
#include "DeviceIdentity.h"

namespace
{
const char *operationSuffix(ResourceTopicOperation operation)
{
    switch (operation)
    {
    case ResourceTopicOperation::SET:
        return "set";
    case ResourceTopicOperation::INVOKE:
        return "invoke";
    case ResourceTopicOperation::STATE:
    default:
        return "state";
    }
}
}

// Read at resolution time rather than copied into the resource, so a managed
// resource can never carry a stale name for this device.
const String &resolveResourceOwner(const NetResource &resource)
{
    return resource.isOwned() ? gDeviceIdentity.getDeviceName() : resource.ownerDevice_.deviceName;
}

const String &NetResource::owner() const
{
    return resolveResourceOwner(*this);
}

String resolveResourceManifestTopic(const String &deviceName)
{
    return deviceName + "/manifest";
}

String resolveResourceManifestTopic(const String &deviceName, ManifestFormat format)
{
    String topic = resolveResourceManifestTopic(deviceName);
    if (format == ManifestFormat::MSGPACK)
        topic += "/msgpack";
    return topic;
}

String resolveResourceConsumeManifestTopic(const String &deviceName, ManifestFormat format)
{
    String topic = resolveResourceManifestTopic(deviceName);
    topic += "/consume";
    if (format == ManifestFormat::MSGPACK)
        topic += "/msgpack";
    return topic;
}

String resolveResourceRootTopic(const String &deviceName)
{
    return deviceName + "/resource";
}

// Built from the resource root, not from the manifest topic. They were the same
// string once, and a resource address silently following a change to where the
// manifest lives is exactly the coupling that separating them was meant to end.
String resolveResourceTopic(const String &deviceName, const String &resourceName,
                            ResourceTopicOperation operation)
{
    String topic = resolveResourceRootTopic(deviceName);
    topic += '/';
    topic += resourceName;
    topic += '/';
    topic += operationSuffix(operation);
    return topic;
}

String resolveResourceTopic(const NetResource &resource, ResourceTopicOperation operation)
{
    return resolveResourceTopic(resource.owner(),
                                resource.isRemote() ? resource.sourceResource() : resource.name(),
                                operation);
}

// Retargets a REMOTE resource. The role is fixed at declaration, so ownership
// is deliberately not recalculated from the new device name: what this object
// is and what it currently points at are separate questions.
bool NetResource::setRemoteSource(const String &deviceName, const String &resourceName)
{
#if NM_ENABLE_RESOURCES
    ResourcesManager *manager = resourceManager_ != nullptr ? resourceManager_ : &gResourcesManager;
    return manager->configureRemoteSource(*this, deviceName, resourceName);
#else
    ownerDevice_ = NetDeviceIdentity(deviceName);
    sourceResourceName_ = resourceName;
    resetRemoteState();
    return true;
#endif
}

bool NetResource::clearRemoteSource()
{
    return setRemoteSource(String(), String());
}

bool NetResource::addDependency(const NetResource &input)
{
    // A resource cannot be its own input, and the manifest would publish an
    // edge no reader could do anything with.
    if (&input == this)
    {
        LOG_WARNING("NET", "Resource '%s' cannot depend on itself", name_.c_str());
        return false;
    }
    for (uint8_t i = 0; i < dependencyCount_; ++i)
    {
        if (dependencies_[i] == &input)
            return true; // Already declared; saying it twice is not an error.
    }
    if (dependencyCount_ >= NetResourceMaxDependencies)
    {
        LOG_WARNING("NET", "Resource '%s' already has %u dependencies; '%s' was not added",
                    name_.c_str(), (unsigned)NetResourceMaxDependencies, input.name_.c_str());
        return false;
    }
    dependencies_[dependencyCount_++] = &input;
#if NM_ENABLE_RESOURCES
    // Declared before binding in the usual setup order, in which case the
    // manifest has not been published yet and this does nothing.
    if (resourceManager_ != nullptr)
        resourceManager_->publishManifest();
#endif
    return true;
}

void NetValueResource::resetRemoteState()
{
    freshness_ = ResourceFreshness::UNKNOWN;
    hasAuthoritativeValue_ = false;
    hasOptimisticValue_ = false;
    lastUpdateMs_ = 0;
    lastWriteMs_ = 0;
}

// Timing uses millis() rather than epoch time: it is monotonic, available
// before any time sync, and unsigned subtraction already handles rollover.
bool NetValueResource::optimisticActive() const
{
    if (syncStrategy_ != NetSyncStrategy::OPTIMISTIC || !hasOptimisticValue_)
        return false;
    return (uint32_t)(millis() - lastWriteMs_) < optimisticWindowMs_;
}

void NetValueResource::noteLocalWrite()
{
    lastWriteMs_ = (uint32_t)millis();
    hasOptimisticValue_ = true;
}

void NetValueResource::noteOwnerUpdate()
{
    lastUpdateMs_ = (uint32_t)millis();
    freshness_ = ResourceFreshness::FRESH;
}

bool NetValueResource::dispatchLocalWrite(const String &encoded)
{
    if (!isOwned() && access_ != AccessPolicy::READ_WRITE)
    {
        LOG_WARNING("NET", "Attempt to set value of read-only resource '%s' owned by '%s'",
                    name_.c_str(), ownerDevice_.deviceName.c_str());
        return false;
    }
    // Checked here, bound or not, so a value set while unbound cannot later
    // turn out to be unpublishable. An empty encoding is the wire's deletion
    // marker and can never be a real value.
    if (encoded.length() == 0 || encoded.length() > NetResourceMaxPayloadLength)
    {
        LOG_WARNING("NET", "Rejected value for '%s': encoded length %u is outside 1..%u",
                    name_.c_str(), (unsigned)encoded.length(), (unsigned)NetResourceMaxPayloadLength);
        return false;
    }
#if NM_ENABLE_RESOURCES
    if (resourceManager_ != nullptr)
        return resourceManager_->setValue(*this, encoded);
#else
    (void)encoded;
#endif
    // An unbound local value is just local state and the write succeeded. A
    // write aimed at another device did not: reporting success here would let
    // the optimistic window show a value that was never transported.
    if (!isOwned())
    {
        LOG_WARNING("NET", "Cannot set unbound remote value '%s' owned by '%s'",
                    name_.c_str(), ownerDevice_.deviceName.c_str());
        return false;
    }
    return true;
}

bool NetActionResource::dispatchInvoke(const String &payload)
{
#if NM_ENABLE_RESOURCES
    if (resourceManager_ != nullptr)
        return resourceManager_->invoke(*this, payload);
#else
    (void)payload;
#endif
    // Invoking always means reaching the implementing device, so without a
    // transport there is nothing to report success about.
    LOG_WARNING("NET", "Cannot invoke unbound action '%s' owned by '%s'",
                name_.c_str(), ownerDevice_.deviceName.c_str());
    return false;
}
