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
    return deviceName + "/resources";
}

String resolveResourceManifestTopic(const String &deviceName, ManifestFormat format)
{
    String topic = resolveResourceManifestTopic(deviceName);
    // One segment below the manifest, which cannot collide with a resource:
    // a value lives at resources/<name>/state, three segments deep, and this is
    // two. A device may still own a resource called "msgpack" without either
    // topic shadowing the other.
    if (format == ManifestFormat::MSGPACK)
        topic += "/msgpack";
    return topic;
}

String resolveResourceTopic(const String &deviceName, const String &resourceName,
                            ResourceTopicOperation operation)
{
    String topic = resolveResourceManifestTopic(deviceName);
    topic += '/';
    topic += resourceName;
    topic += '/';
    topic += operationSuffix(operation);
    return topic;
}

String resolveResourceTopic(const NetResource &resource, ResourceTopicOperation operation)
{
    return resolveResourceTopic(resource.owner(), resource.name(), operation);
}

// Retargets a REMOTE resource. The role is fixed at declaration, so ownership
// is deliberately not recalculated from the new device name: what this object
// is and what it currently points at are separate questions.
void NetResource::setRemoteSource(const String &deviceName, const String &resourceName)
{
    // Captured before the swap: the Manager still has the old source subscribed
    // and cannot reconstruct those topics once they are overwritten.
    const NetDeviceIdentity oldOwner = ownerDevice_;
    const String oldName = name_;

    ownerDevice_ = NetDeviceIdentity(deviceName);
    name_ = resourceName;
    resetRemoteState();
#if NM_ENABLE_RESOURCES
    if (resourceManager_ != nullptr)
    {
        resourceManager_->notifySourceChanged(*this, oldOwner, oldName);
    }
#endif
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
