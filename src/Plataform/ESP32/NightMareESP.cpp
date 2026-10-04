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
#if NM_ENABLE_WIFI_RADIO
#include <Network/WiFiRadio/NmWifiRadioService.h>
#endif
#if NM_ENABLE_WIFI
#include <Network/WiFiIP/NmWifiService.h>
#endif
#if NM_ENABLE_MQTT
#include <Network/MQTT/NmMqttConnection.h>
#endif
#if NM_NETWORK_ESPNOW
#include <Network/EspNow/NmEspNowConnection.h>
#endif
#if NM_ENABLE_TIME_SYNC
#include <Util/TimeSyncronization.h>
#endif
#if NM_ENABLE_NETWORK
#include <Network/IdentityCleanup.h>
#endif
#if NM_ENABLE_NETWORK
#include <Network/NmConnection.h>
#include <Network/NmConnectionInternal.h>
#endif

namespace
{
    struct RequestRetry
    {
        uint32_t delayMs = 0;
        uint32_t retryAtMs = 0;
        bool scheduled = false;
    };

    const char *SystemRequestNames[] = {
        "PublishStatus",
        "PublishManifest",
        "PublishConsumeManifest",
        "PublishResourceStates",
        "PublishInfo",
        "PublishHardware",
        "PublishTelemetry",
        "Count"};

    RequestRetry requestRetries[SystemRequestCount];

    bool retryReady(const RequestRetry &retry, uint32_t now)
    {
        return retry.scheduled && static_cast<int32_t>(now - retry.retryAtMs) >= 0;
    }

    uint32_t nextRetryDelay(uint32_t previous)
    {
        if (previous == 0)
            return NM_SYSTEM_REQUEST_RETRY_MS;
        if (previous >= NM_SYSTEM_REQUEST_MAX_RETRY_MS / 2)
            return NM_SYSTEM_REQUEST_MAX_RETRY_MS;
        const uint32_t doubled = previous * 2;
        return doubled < NM_SYSTEM_REQUEST_MAX_RETRY_MS
                   ? doubled
                   : NM_SYSTEM_REQUEST_MAX_RETRY_MS;
    }

    bool processSystemRequest(SystemRequest request)
    {
#if NM_ENABLE_NETWORK
        if (NightMare::GetConnectionState() != NightMare::ConnectionState::CONNECTED &&
            request != SystemRequest::Count)
            return false;
#endif

        // These publish through NmConnection, whichever connection is active --
        // MQTT or ESP-NOW -- so they depend on NM_ENABLE_NETWORK, not the MQTT
        // driver. Guarded on NM_ENABLE_MQTT they returned "done" without
        // publishing on an ESP-NOW-only build: no status, no consume manifest.
        switch (request)
        {
        case SystemRequest::PublishStatus:
#if NM_ENABLE_NETWORK
            return NightMare::PublishText(gDeviceIdentity.topic("status"), NightMare::ConnectionDeviceStatusJson(true), true);
#else
            return true;
#endif
        case SystemRequest::PublishManifest:
#if NM_ENABLE_NETWORK
            return  gResourcesManager.publishManifest();
#else
            return true;
#endif
        case SystemRequest::PublishConsumeManifest:
#if NM_ENABLE_NETWORK
            return gResourcesManager.publishConsumeManifest();
#else
            return true;
#endif
        case SystemRequest::PublishResourceStates:
#if NM_ENABLE_NETWORK
            return gResourcesManager.publishResourceStates();
#else
            return true;
#endif
        case SystemRequest::PublishInfo:
#if NM_ENABLE_TELEMETRY
            return  Telemetry.publishInfo(InfoType::INFO);
#else
            return true;
#endif
        case SystemRequest::PublishHardware:
#if NM_ENABLE_TELEMETRY
            return Telemetry.publishHardware();
#else
            return true;
#endif
        case SystemRequest::PublishTelemetry:
#if NM_ENABLE_TELEMETRY
            {
                // INFO and hardware have their own requests.
                const bool system = Telemetry.publishInfo(InfoType::SYSTEM);
                const bool network = Telemetry.publishInfo(InfoType::NETWORK);
                const bool heartbeat = !HeartbeatEnabled.value() ||
                                       Telemetry.publishInfo(InfoType::HEARTBEAT);
                return system && network && heartbeat;
            }
#else
            return true;
#endif
        case SystemRequest::Count:
            return true;
        }
        return true;
    }

    String debugSystemRequest(int start = 0)
    {
        
        String result = "";
        for (size_t i = 0; i < SystemRequestCount; ++i)
        {
            int index = (start + i) % SystemRequestCount;
            if (SystemState.pending(static_cast<SystemRequest>(index)) ||
                requestRetries[index].scheduled)
            {
                if (!result.isEmpty())
                    result += ", ";
                result += SystemRequestNames[index];
            }
        }
        return result;
    }

    void processOneSystemRequest()
    {
        static uint16_t next = 0;
#if NM_ENABLE_NETWORK
        // All current requests publish through the active connection. Stay idle while offline;
        // an expired delay becomes ready after reconnect, without a busy loop.
        if (NightMare::GetConnectionState() != NightMare::ConnectionState::CONNECTED)
            return;
#endif
        const uint32_t now = millis();
        for (size_t checked = 0; checked < SystemRequestCount; ++checked)
        {
            const uint16_t index = (next + checked) % SystemRequestCount;
            const SystemRequest request = static_cast<SystemRequest>(index);
            // Serial.printf("Processing system request: %d  queue: [%s]\n", index, debugSystemRequest().c_str());
            RequestRetry &retry = requestRetries[index];
            const bool fresh = SystemState.pending(request);
            if (fresh)
            {
                if (!SystemState.take(request))
                    continue;
                // A new request supersedes an older failed attempt and starts
                // this request's backoff sequence again from zero.
                retry = RequestRetry{};
            }
            else if (!retryReady(retry, now))
            {
                continue;
            }
            next = (index + 1) % SystemRequestCount;
            if (processSystemRequest(request))
            {
                retry = RequestRetry{};
            }
            else
            {
                retry.delayMs = nextRetryDelay(retry.delayMs);
                retry.retryAtMs = now + retry.delayMs;
                retry.scheduled = true;
                LOG_WARNING("NM", "System request \x1b[91m%s\x1b[0m failed; retrying in %lu ms",
                            SystemRequestNames[index],
                            static_cast<unsigned long>(retry.delayMs));
            }
            return;
        }
    }
}
#if NM_ENABLE_CONSOLE && NM_CONSOLE_SERIAL
#include <Core/NightMareCommand.h>
#endif

#if NM_ENABLE_SCHEDULER && NM_ENABLE_NETWORK
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
    if (!configManager().restore())
        LOG_ERROR("NM", "Could not restore persistent Config values");
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
    if (!configManager().restore())
        LOG_ERROR("NM", "Could not restore persistent Config values");
#if NM_ENABLE_RESOURCES
    if (!gResourcesManager.bindEnlisted())
        LOG_ERROR("NM", "One or more declared Resources could not be bound");
    if (!gResourcesManager.loadResourceSettings())
        LOG_ERROR("NM", "Could not load Resource advertisement settings");
    if (!gResourcesManager.loadRemoteSources())
        LOG_ERROR("NM", "Could not load Remote Resource sources");
#endif
#if NM_ENABLE_SCHEDULER
    if (!gScheduler.begin(NM_SCHEDULER_OWN_TASK ? SchedulerRunMode::TASK
                                                : SchedulerRunMode::MANUAL))
        LOG_ERROR("NM", "Scheduler did not start; no job will run");
#if NM_ENABLE_NETWORK
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
#if NM_ENABLE_WIFI_RADIO
    // The radio first: ESP-NOW needs nothing more and starts from its ingress.
    if (!NightMare::WiFiRadioBegin())
        LOG_ERROR("NM", "Wi-Fi radio did not start; ESP-NOW and the IP station cannot run");
#endif
#if NM_ENABLE_WIFI && NM_WIFI_AUTO
    // Each connectivity service starts independently; preference does not
    // decide which services exist or run.
    if (!NightMare::WiFiBegin())
        LOG_ERROR("NM", "Wi-Fi station did not start");
#endif
#if NM_NETWORK_ESPNOW
    if (!NightMare::EspNow_enable())
        LOG_ERROR("NM", "ESP-NOW service did not start");
#endif
#if NM_ENABLE_MQTT && NM_WIFI_AUTO
    {
        NightMare::ConnectionType profile = NightMare::GetPreferredConnection();
        if (profile != NightMare::ConnectionType::MQTT &&
            profile != NightMare::ConnectionType::LOCAL_MQTT)
        {
#if NM_NETWORK_MQTT
            profile = NightMare::ConnectionType::MQTT;
#else
            profile = NightMare::ConnectionType::LOCAL_MQTT;
#endif
        }
        if (!NightMare::Mqtt_enable(profile))
            LOG_ERROR("NM", "MQTT service did not start");
    }
#endif
#if NM_ENABLE_NETWORK
    // Routing observes the services above; it never starts or stops them.
    NightMare::ConnectionBegin();
#endif
}

void tickNightMareESP()
{
#if NM_ENABLE_RESOURCES
    gResourcesManager.tick();
#endif
#if NM_ENABLE_TIME_SYNC
    processTimeSyncEvents();
#endif
    processOneSystemRequest();
#if NM_ENABLE_NETWORK
    NightMare::ConnectionTick();
#endif
#if NM_ENABLE_WIFI
    NightMare::WiFiIP_tick();
#endif
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
