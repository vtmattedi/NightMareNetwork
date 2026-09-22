#include <NightMare/Features.h>
#if NM_ENABLE_MQTT
#include "IdentityCleanup.h"
#include "MQTT.h"

#include <Core/DeviceIdentity.h>
#include <Core/ResourcesManager.h>

IdentityCleanupResult processPendingIdentityCleanup()
{
    if (!gDeviceIdentity.hasPendingIdentityCleanup())
        return IdentityCleanupResult::NOTHING_PENDING;

    PendingIdentityCleanup cleanup;
    // Pending but not yet actionable: this boot still runs as the old name.
    if (!gDeviceIdentity.getPendingIdentityCleanup(cleanup))
        return IdentityCleanupResult::PENDING;

    if ((cleanup.pendingFlags & CLEANUP_RESOURCES) != 0 &&
        gResourcesManager.withdrawIdentity(cleanup.oldName))
        gDeviceIdentity.markIdentityCleanupComplete(CLEANUP_RESOURCES);

    if ((cleanup.pendingFlags & CLEANUP_STATUS) != 0)
    {
        // The usual offline status first, so anyone watching sees the old name
        // go away in the normal format, then empty to delete the retained
        // status so it does not linger as a ghost.
        const String statusTopic = cleanup.oldName + "/status";
        if (MQTT_Publish(statusTopic, deviceStatusJson(cleanup.oldName, false), false, true) &&
            MQTT_Publish(statusTopic, "", false, true))
            gDeviceIdentity.markIdentityCleanupComplete(CLEANUP_STATUS);
    }

    return gDeviceIdentity.hasPendingIdentityCleanup() ? IdentityCleanupResult::PENDING
                                                       : IdentityCleanupResult::COMPLETE;
}
#endif // NM_ENABLE_MQTT
