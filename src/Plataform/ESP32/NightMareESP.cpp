#include "NightMareESP.h"

#include <Core/DeviceIdentity.h>
#if NM_ENABLE_SCHEDULER
#include <Core/Scheduler.h>
#endif
#if NM_ENABLE_TELEMETRY
#include <Core/Telemetry.h>
#endif
#if NM_ENABLE_WIFI
#include "NightMareWIFI.h"
#endif
#if NM_ENABLE_MQTT
#include <Network/IdentityCleanup.h>
#endif

#if NM_ENABLE_SCHEDULER && NM_ENABLE_MQTT
namespace
{
constexpr char IdentityCleanupJob[] = "_nm_identity_cleanup";

void identityCleanupTask()
{
    // PENDING keeps the job for the next interval: offline, a failed part, or
    // an adoption that only takes effect after reboot.
    if (processPendingIdentityCleanup() != IdentityCleanupResult::PENDING)
        gScheduler.remove(IdentityCleanupJob);
}
}
#endif

// The application binds its resources before calling this, which is why the
// cleanup retry is installed here: withdrawal can only reach resources that are
// already declared.
void startNightMareESP()
{
    gDeviceIdentity.begin();
#if NM_ENABLE_SCHEDULER
    gScheduler.begin();
#if NM_ENABLE_MQTT
    if (gDeviceIdentity.hasPendingIdentityCleanup() &&
        gScheduler.timer(IdentityCleanupJob, identityCleanupTask, NM_IDENTITY_CLEANUP_RETRY_MS) < 0)
        LOG_ERROR("NM", "Could not schedule cleanup of the previous identity; "
                        "processPendingIdentityCleanup() can still be called directly");
#endif
#endif
#if NM_ENABLE_TELEMETRY
    Telemetry.start();
#endif
#if NM_ENABLE_WIFI
    Serial.println("Initializing WiFi...");
    WiFi_Auto();
#endif
}
