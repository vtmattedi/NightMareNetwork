#include "NetResources.h"
#include <NightMare/Features.h>
#if NM_ENABLE_RESOURCES
#include "ResourcesManager.h"
#endif

bool NetValueResource::setValue(const String &newValue)
{
#if NM_ENABLE_RESOURCES
    if (resourceManager != nullptr)
        return resourceManager->setValue(*this, newValue);
#endif

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
#if NM_ENABLE_RESOURCES
    if (resourceManager != nullptr)
        return resourceManager->invoke(*this, payload);
#endif

    if (authority != ResourceAuthority::THIS_DEVICE || access != NetAccess::READ_WRITE || onInvoke == nullptr)
        return false;
    return onInvoke(*this, payload);
}
