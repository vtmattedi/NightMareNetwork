#include "NetResources.h"
#include <NightMare/Features.h>
#if NM_ENABLE_RESOURCES
#include "ResourcesManager.h"
#endif

#include <Core/DeviceIdentity.h>

// Safely Change Device Identity.
//  This will change the ownership/topic reference and notify the resource manager .
bool NetResource::setDeviceIdentity(const String &resourceName, const String &resoruceOwner)
{
    this->name = resourceName;
    NetDeviceIdentity newOwner(resoruceOwner);
    bool isOwner = gDeviceIdentity.isDevice(newOwner.deviceName);
    this->ownerDevice = newOwner;
    this->isOwned_ = isOwner;
#if NM_ENABLE_RESOURCES
    if (resourceManager != nullptr)
    {
        resourceManager->notifyOwnershipChanged(*this);
    }
#endif
    return true;
}

// Timing uses millis() rather than epoch time: it is monotonic, available
// before any time sync, and unsigned subtraction already handles rollover.
bool NetValueResource::optimisticActive() const
{
    if (syncStrategy != NetSyncStrategy::OPTIMISTIC || !hasOptimisticValue_)
        return false;
    return (uint32_t)(millis() - lastWriteMs_) < optimisticWindowMs;
}

void NetValueResource::noteLocalWrite()
{
    lastWriteMs_ = (uint32_t)millis();
    hasOptimisticValue_ = true;
}

void NetValueResource::noteOwnerUpdate()
{
    lastUpdateMs_ = (uint32_t)millis();
    freshness = ResourceFreshness::FRESH;
}

bool NetValueResource::dispatchLocalWrite(const String &encoded)
{
    if (!isOwned() && access != AccessPolicy::READ_WRITE)
    {
        LOG_WARNING("NET", "Attempt to set value of read-only resource '%s' owned by '%s'",
                    name.c_str(), ownerDevice.deviceName.c_str());
        return false;
    }
#if NM_ENABLE_RESOURCES
    if (resourceManager != nullptr)
        return resourceManager->setValue(*this, encoded);
#else
    (void)encoded;
#endif
    // An unbound local value is just local state and the write succeeded. A
    // write aimed at another device did not: reporting success here would let
    // the optimistic window show a value that was never transported.
    if (!isOwned())
    {
        LOG_WARNING("NET", "Cannot set unbound remote value '%s' owned by '%s'",
                    name.c_str(), ownerDevice.deviceName.c_str());
        return false;
    }
    return true;
}

bool NetActionResource::dispatchInvoke(const String &payload)
{
#if NM_ENABLE_RESOURCES
    if (resourceManager != nullptr)
        return resourceManager->invoke(*this, payload);
#else
    (void)payload;
#endif
    // Invoking always means reaching the implementing device, so without a
    // transport there is nothing to report success about.
    LOG_WARNING("NET", "Cannot invoke unbound action '%s' owned by '%s'",
                name.c_str(), ownerDevice.deviceName.c_str());
    return false;
}
