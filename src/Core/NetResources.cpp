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
    case ResourceTopicOperation::EVENT:
        return "event";
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

String resolveResourceManifestTopic(const String &deviceName, DocumentFormat format)
{
    return resolveDocumentTopic(deviceName, "manifest", format);
}

String resolveResourceConsumeManifestTopic(const String &deviceName, DocumentFormat format)
{
    return resolveDocumentTopic(deviceName, "manifest/consume", format);
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

NetResource *&NetResource::enlistedHead()
{
    static NetResource *head = nullptr; // function-local: constructed on first use
    return head;
}

void NetResource::enlistForBinding()
{
    nextEnlisted_ = enlistedHead();
    enlistedHead() = this;
    enlisted_ = true;
}

void NetResource::delistFromBinding()
{
    if (!enlisted_)
        return;
    for (NetResource **link = &enlistedHead(); *link != nullptr; link = &(*link)->nextEnlisted_)
    {
        if (*link == this)
        {
            *link = nextEnlisted_;
            break;
        }
    }
    enlisted_ = false;
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

bool NetValueResource::setDependency(const NetValueResource &source)
{
    // A value cannot mirror itself, and the manifest would publish an edge no
    // reader could do anything with.
    if (&source == this)
    {
        LOG_WARNING("NET", "Resource '%s' cannot depend on itself", name_.c_str());
        return false;
    }
    // Mirroring copies the encoded value straight across, so the two have to
    // agree on what the encoding means. This is the only part of "these are the
    // same value" that can be checked here; the rest is the caller's claim.
    if (source.valueType_ != valueType_)
    {
        LOG_WARNING("NET", "Resource '%s' cannot depend on '%s': value type %u is not %u",
                    name_.c_str(), source.name_.c_str(), (unsigned)source.valueType_,
                    (unsigned)valueType_);
        return false;
    }
    dependency_ = &source; // One dependency: the last call wins.
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
    available_ = false;
    advertisementPolicyKnown_ = false;
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
    if (!isOwned())
        available_ = true;
}

bool NetValueResource::setManagedAvailability(bool available)
{
    if (!isOwned())
        return false;
#if NM_ENABLE_RESOURCES
    if (resourceManager_ != nullptr)
        return resourceManager_->setAvailability(*this, available);
#endif
    available_ = available;
    withdrawalPending_ = !available;
    return true;
}

bool NetValueResource::setManagedHardwarePolicy(const HardwarePolicy &policy)
{
    if (!isOwned() || policy.note.length() > NetResourceHardwareNoteMaxLength)
        return false;
#if NM_ENABLE_RESOURCES
    if (resourceManager_ != nullptr)
        return resourceManager_->setHardwarePolicy(*this, policy);
#endif
    hardwarePolicy_ = policy;
    hardwareDefaultPollMs_ = policy.pollMs;
    hardwarePolicyDeclared_ = true;
    return true;
}

bool NetValueResource::setManagedHardwareConnected(bool connected)
{
    if (!isOwned() || !hardwarePolicyDeclared_ ||
        (hardwarePolicy_.flags & REPORT_HW_CONNECTION) == 0)
        return false;
#if NM_ENABLE_RESOURCES
    if (resourceManager_ != nullptr)
        return resourceManager_->setHardwareConnected(*this, connected);
#endif
    hardwareConnected_ = connected;
    return true;
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
    // connection there is nothing to report success about.
    LOG_WARNING("NET", "Cannot invoke unbound action '%s' owned by '%s'",
                name_.c_str(), ownerDevice_.deviceName.c_str());
    return false;
}

bool NetEventResource::dispatchFire(const String &encoded)
{
    // Checked here, bound or not, so the answer does not depend on the connection.
    // An empty payload carries nothing a listener could decode.
    if (encoded.length() == 0 || encoded.length() > NetResourceMaxPayloadLength)
    {
        LOG_WARNING("NET", "Rejected event '%s': encoded length %u is outside 1..%u",
                    name_.c_str(), (unsigned)encoded.length(), (unsigned)NetResourceMaxPayloadLength);
        return false;
    }
#if NM_ENABLE_RESOURCES
    if (resourceManager_ != nullptr)
    {
        if (!resourceManager_->fire(*this, encoded))
            return false;
        // Only an occurrence the transport took counts as having been fired.
        noteOccurrence();
        return true;
    }
#endif
    // Firing always means reaching the transport; unbound, nothing was sent.
    LOG_WARNING("NET", "Cannot fire unbound event '%s'", name_.c_str());
    return false;
}
