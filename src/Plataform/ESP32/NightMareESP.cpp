#include "NightMareESP.h"
#include <NightMare.h>
#include <Core/DeviceIdentity.h>
#include <Core/SystemState.h>
#if NM_ENABLE_RESOURCES
#include <Core/ResourcesManager.h>
#endif
#if NM_ENABLE_SCHEDULER
#include <Core/Scheduler.h>
#endif
#if NM_ENABLE_TELEMETRY
#include <Core/Telemetry.h>
#endif
#if NM_ENABLE_WIFI
#include "NightMareWIFI.h"
#endif
#if NM_ENABLE_TIME_SYNC
#include <Util/TimeSyncronization.h>
#endif
#if NM_ENABLE_MQTT
#include <Network/IdentityCleanup.h>
#include <Network/MQTT.h>
#endif

namespace
{
bool processSystemRequest(SystemRequest request)
{
    switch (request)
    {
    case SystemRequest::PublishStatus:
#if NM_ENABLE_MQTT
        return MQTT_Connected() && MQTT_Publish("status", deviceStatusJson(true), true, true);
#else
        return true;
#endif
    case SystemRequest::PublishManifest:
#if NM_ENABLE_MQTT
        return MQTT_Connected() && gResourcesManager.publishManifest();
#else
        return true;
#endif
    case SystemRequest::PublishConsumeManifest:
#if NM_ENABLE_MQTT
        return MQTT_Connected() && gResourcesManager.publishConsumeManifest();
#else
        return true;
#endif
    case SystemRequest::PublishResourceStates:
#if NM_ENABLE_MQTT
        return MQTT_Connected() && gResourcesManager.publishResourceStates();
#else
        return true;
#endif
    case SystemRequest::PublishInfo:
#if NM_ENABLE_TELEMETRY
        return MQTT_Connected() && Telemetry.publishInfo(InfoType::INFO);
#else
        return true;
#endif
    case SystemRequest::PublishHardwareJson:
#if NM_ENABLE_TELEMETRY
        return MQTT_Connected() && Telemetry.publishHardware(HardwareFormat::JSON);
#else
        return true;
#endif
    case SystemRequest::PublishHardwareMsgPack:
#if NM_ENABLE_TELEMETRY
        return MQTT_Connected() && Telemetry.publishHardware(HardwareFormat::MSGPACK);
#else
        return true;
#endif
    case SystemRequest::Count:
        return true;
    }
    return true;
}

void processOneSystemRequest()
{
    static uint16_t next = 0;
    for (size_t checked = 0; checked < SystemRequestCount; ++checked)
    {
        const uint16_t index = (next + checked) % SystemRequestCount;
        const SystemRequest request = static_cast<SystemRequest>(index);
        if (!SystemState.take(request))
            continue;
        next = (index + 1) % SystemRequestCount;
        if (!processSystemRequest(request))
            SystemState.request(request);
        return;
    }
}
}
#if NM_ENABLE_CONSOLE && NM_CONSOLE_SERIAL
#include <Core/NightMareCommand.h>
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

void introNightMareESP()
{
    Serial.begin(115200);
    gDeviceIdentity.begin();
    Serial.print(MattediWorksPresents);
    Serial.print(NightMareNetworkFiglet);
    Serial.println(gDeviceIdentity.getDeviceName());
#ifdef VERSION
    // Device projects normally generate VERSION and BUILD_TIMESTAMP from their
    // version pre-script. Guards keep standalone library builds usable too.
    Serial.printf("\tFirmware Version: %s\n", VERSION);
#endif
#ifdef BUILD_TIMESTAMP
    Serial.printf("\tBuild Date: %s\n", BUILD_TIMESTAMP);
#endif
}

// The application binds its resources before calling this, which is why the
// cleanup retry is installed here: withdrawal can only reach resources that are
// already declared.
void startNightMareESP()
{
    gDeviceIdentity.begin();
#if NM_ENABLE_SCHEDULER
    if (!gScheduler.begin(NM_SCHEDULER_OWN_TASK ? SchedulerRunMode::TASK
                                                : SchedulerRunMode::MANUAL))
        LOG_ERROR("NM", "Scheduler did not start; no job will run");
#if NM_ENABLE_MQTT
    if (gDeviceIdentity.hasPendingIdentityCleanup() &&
        gScheduler.timer(IdentityCleanupJob, identityCleanupTask, NM_IDENTITY_CLEANUP_RETRY_MS) < 0)
        LOG_ERROR("NM", "Could not schedule cleanup of the previous identity; "
                        "processPendingIdentityCleanup() can still be called directly");
#endif
#endif
#if NM_ENABLE_TELEMETRY
    if (!Telemetry.start())
        LOG_ERROR("NM", "Could not schedule periodic telemetry");
#endif
#if NM_ENABLE_WIFI
    Serial.println("Initializing WiFi...");
    WiFi_Auto();
#endif
}

void tickNightMareESP()
{
#if NM_ENABLE_TIME_SYNC
    processTimeSyncEvents();
#endif
    processOneSystemRequest();
#if NM_ENABLE_SCHEDULER
    // In TASK mode the Scheduler's own task does this; ticking here too would
    // only contend for the same lock.
    if (gScheduler.runMode() == SchedulerRunMode::MANUAL)
        gScheduler.tick();
#endif
#if NM_ENABLE_CONSOLE && NM_CONSOLE_SERIAL
    NightMareCommand_SerialResolver(&Serial, '\n');
#endif
}
