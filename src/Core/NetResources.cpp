#include "NetResources.h"
#include <NightMare/Features.h>
#if NM_ENABLE_RESOURCES
#include "ResourcesManager.h"
#endif

#include <Core/Time.h>

// Safely Change Device Identity.
//  This will change the ownership/topic reference and notify the resource manager .
bool NetResource::setDeviceIdentity(const String &resourceName, const String &resoruceOwner)
{
    this->name = resourceName;
    NetDeviceIdentity newOwner(resoruceOwner);
    bool isOwner = gDeviceIdentity.isDevice(newOwner.deviceName);
    this->ownerDevice = newOwner;
    this->isOwned_ = isOwner;
    if (resourceManager != nullptr)
    {
        resourceManager->notifyOwnershipChanged(*this);
    }
    return true;
}

NetSensor::NetSensor(const String &resourceName, const NetDeviceIdentity &owner, const ResourceAuthority resourceAuthority, const AccessPolicy resourceAccess)
    : NetValueResource(resourceName, owner, resourceAuthority, resourceAccess) {
      };

bool NetValueResource::setValue(const String &newValue, bool skipManager)
{
    if (!this->isOwned() && this->access != AccessPolicy::READ_WRITE)
    {
        LOG_WARNING("NET", "Attempt to set value of read-only resource '%s' owned by '%s'", name.c_str(), ownerDevice.deviceName.c_str());
        return false;
    }
    // LOG("NET", "%sSetting value of resource '%s' to '%s'",OK_LOG(resourceManager != nullptr), name.c_str(), newValue.c_str());
#if NM_ENABLE_RESOURCES
    if (resourceManager != nullptr && !skipManager)
        return resourceManager->setValue(*this, newValue);
#endif
    value = newValue;
    freshness = ResourceFreshness::FRESH;
    return true;
};

bool NetSensor::setValue(const String &newValue, bool isFromOwner)
{
    bool ok = NetValueResource::setValue(newValue); // this already triggers the resource manager.
    if (!ok)
        return false;
    unsigned long timestamp = now();
    if (isFromOwner)
    {
        this->lastUpdateTimestamp = timestamp;
        this->freshness = ResourceFreshness::FRESH;
    }
    else
    {
        if (this->syncStrategy == NetSensorSyncStrategy::OPTMISTIC)
        {
            this->optimisticValue = newValue;
        }
        this->lastWriteTimestamp = timestamp;
    }
    return true;
}

String NetSensor::getValue()
{
    if (this->syncStrategy == NetSensorSyncStrategy::OPTMISTIC)
    {
        bool use_optimistic_value = now() - this->lastWriteTimestamp < NM_NET_SENSOR_OPTIMISTIC_ADJUST_TIME;
        return use_optimistic_value ? this->optimisticValue : this->getValue();
    }
    else
    {
        return this->getValue();
    }
}

bool NetActionResource::invoke(const String &payload)
{
#if NM_ENABLE_RESOURCES
    if (resourceManager != nullptr)
        return resourceManager->invoke(*this, payload);
#endif

    if (authority != ResourceAuthority::HAS_AUTHORITY || access != AccessPolicy::READ_WRITE || onInvoke == nullptr)
        return false;
    return onInvoke(*this, payload);
}
