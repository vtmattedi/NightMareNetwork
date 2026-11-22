#include <NightMare/Features.h>
#if NM_ENABLE_NETWORK
#include "IdentityCleanup.h"
#include "NmConnection.h"
#include "NmConnectionInternal.h"

#include <Core/DeviceIdentity.h>
#include <Core/ResourcesManager.h>
#if NM_GATEWAY_MQTT
#include <Gateway/EspMqtt/espMqtt.h>
#endif

IdentityCleanupResult processPendingIdentityCleanup()
{
    if (!gDeviceIdentity.hasPendingIdentityCleanup())
        return IdentityCleanupResult::NOTHING_PENDING;

    PendingIdentityCleanup cleanup;
    // Pending but not yet actionable: this boot still runs as the old name.
    if (!gDeviceIdentity.getPendingIdentityCleanup(cleanup))
        return IdentityCleanupResult::PENDING;
#if NM_GATEWAY_MQTT
    // A gateway accepts a retained publish into its vault even while the
    // uplink is down, and a tombstone made then never reaches the broker:
    // wait for the uplink.
    if (!mqtt_is_connected())
        return IdentityCleanupResult::PENDING;
#endif

    if ((cleanup.pendingFlags & CLEANUP_RESOURCES) != 0 &&
        gResourcesManager.withdrawIdentity(cleanup.oldName))
        gDeviceIdentity.markIdentityCleanupComplete(CLEANUP_RESOURCES);

    if ((cleanup.pendingFlags & CLEANUP_STATUS) != 0)
    {
        // The usual offline status first, so anyone watching sees the old name
        // go away in the normal format, then empty to delete the retained
        // status so it does not linger as a ghost.
        const std::string statusTopic = cleanup.oldName + "/status";
        if (NightMare::PublishText(
                statusTopic,
                NightMare::ConnectionDeviceStatusJson(cleanup.oldName, false, NightMare::GetActiveConnection()), true) &&
            NightMare::PublishText(statusTopic, "", true))
            gDeviceIdentity.markIdentityCleanupComplete(CLEANUP_STATUS);
    }

    if ((cleanup.pendingFlags & CLEANUP_DOCUMENTS) != 0)
    {
        // The framework documents the old address left retained. Each empty
        // retained publish deletes one; an absent one costs nothing.
        static const char *const Documents[] = {
            "info", "hardware/msgpack", "hardware/json", "telemetry/system",
            "telemetry/network", "manifest/json", "manifest/consume/json",
            // A gateway's announcement and client list: left behind, devices
            // would keep choosing a gateway id that no longer exists.
            "gateway/network", "gateway/clients"};
        bool all = true;
        for (const char *document : Documents)
            all = NightMare::PublishText(cleanup.oldName + "/" + document, "", true) && all;
        if (all)
            gDeviceIdentity.markIdentityCleanupComplete(CLEANUP_DOCUMENTS);
    }

    return gDeviceIdentity.hasPendingIdentityCleanup() ? IdentityCleanupResult::PENDING
                                                       : IdentityCleanupResult::COMPLETE;
}
#endif // NM_ENABLE_NETWORK
