#include "NetResources.h"
#include "ResourcesManager.h"

bool NetValueResource::setValue(const String &newValue)
{
    if (resourceManager != nullptr)
        return resourceManager->setValue(*this, newValue);

    // An unbound resource owned by this device may receive its initial value before binding.
    if (authority != ResourceAuthority::THIS_DEVICE ||
        newValue.length() > NetResourceMaxPayloadLength)
        return false;
    value = newValue;
    freshness = ResourceFreshness::FRESH;
    return true;
}

bool NetActionResource::invoke(const String &payload)
{
    if (resourceManager != nullptr)
        return resourceManager->invoke(*this, payload);

    if (authority != ResourceAuthority::THIS_DEVICE || access != NetAccess::READ_WRITE || onInvoke == nullptr)
        return false;
    return onInvoke(*this, payload);
}
