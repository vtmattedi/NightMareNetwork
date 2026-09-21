#include <NightMare/Features.h>
#if NM_ENABLE_MQTT
#include "NmMessageRouter.h"
#include "MQTT.h"

#include <Core/DeviceIdentity.h>
#include <Core/ResourcesManager.h>
#if NM_ENABLE_TELEMETRY
#include <Core/Telemetry.h>
#endif
#if NM_ENABLE_TIME_SYNC
#include <Core/Time.h>
#include <Core/StateStore.h>
#include <Util/TimeSyncronization.h>
#include <ArduinoJson.h>
#include <stdlib.h>
#endif
#if NM_ENABLE_CONSOLE
#include <Core/NightMareCommand.h>
#endif

namespace
{
#if NM_ENABLE_CONSOLE
bool firstConnection = true;
bool validControlId(const String &id)
{
    if (id.length() == 0 || id.length() > 64)
        return false;
    for (size_t i = 0; i < id.length(); ++i)
    {
        const char c = id[i];
        if (c == '/' || c == '+' || c == '#' || static_cast<uint8_t>(c) < 0x20)
            return false;
    }
    return true;
}

void runCommand(const String &payload, const String &replyTopic)
{
    NightmareContext context(NM_CMD_SRC_MQTT, replyTopic);
    NightMareResults result = handleNightMareCommand(payload, context);
    if (result.context.msgSource != NM_CMD_ANS_DO_NOT_RESPOND)
        MQTT_Publish(replyTopic, result.response, false, false);
}

#endif

// Finishes an adoption: removes what the previous identity left retained on the
// broker. Each part clears its own flag only when it succeeded, so a failure is
// retried on the next connection and the record goes once nothing is left.
void runPendingIdentityCleanup()
{
    PendingIdentityCleanup cleanup;
    if (!gDeviceIdentity.getPendingIdentityCleanup(cleanup))
        return;
    LOG("MQTT", "Cleaning up previous identity '%s'", cleanup.oldName.c_str());

    if ((cleanup.pendingFlags & CLEANUP_RESOURCES) != 0 &&
        gResourcesManager.withdrawIdentity(cleanup.oldName))
        gDeviceIdentity.markIdentityCleanupComplete(CLEANUP_RESOURCES);

    if ((cleanup.pendingFlags & CLEANUP_STATUS) != 0)
    {
        // "offline" first so anyone watching sees the old device go away, then
        // empty to delete the retained status so it does not linger as a ghost.
        const String statusTopic = cleanup.oldName + "/status";
        if (MQTT_Publish(statusTopic, "offline", false, true) &&
            MQTT_Publish(statusTopic, "", false, true))
            gDeviceIdentity.markIdentityCleanupComplete(CLEANUP_STATUS);
    }
}

#if NM_ENABLE_TIME_SYNC
bool syncTime(const String &payload)
{
    if (payload.length() > 256)
        return true;
    DynamicJsonDocument doc(256);
    if (deserializeJson(doc, payload) || !doc.containsKey("timestamp") ||
        !doc.containsKey("offset"))
        return true;
    String timestampText;
    serializeJson(doc["timestamp"], timestampText);
    char *end = nullptr;
    double timestamp = strtod(timestampText.c_str(), &end);
    if (end == timestampText.c_str() || *end != '\0')
        return true;
    if (timestamp > 4294967295.0)
        timestamp /= 1000.0;
    if (timestamp <= 0 || timestamp > 4294967295.0)
        return true;
    manualSyncTime(static_cast<uint32_t>(timestamp));
    return true;
}
#endif
}

namespace NmMessageRouter
{
void onConnected()
{
    // Before announcing: the old identity's footprint goes, then the current
    // one is published.
    runPendingIdentityCleanup();
    MQTT_Publish("status", "online", true, true);
    gResourcesManager.announceAll();
#if NM_ENABLE_TELEMETRY
    Telemetry.publish();
#endif
#if NM_ENABLE_CONSOLE
    MQTT_Publish("console/out", firstConnection ? "Booted" : "Connected");
#endif
#if NM_ENABLE_TIME_SYNC
    if (!NightMare::Time::valid())
        MQTT_Publish("Control/request", "time", false, false);
#endif
#if NM_ENABLE_CONSOLE
    firstConnection = false;
#endif
}

bool handleMessage(const String &fullTopic, const String &payload)
{
    // The resource manager accepts owned /set and /invoke requests, as well as
    // manifests and states from other devices. It sees full topics in both cases.
    if (gResourcesManager.handleIngressMessage(fullTopic, payload))
        return true;

#if NM_ENABLE_TIME_SYNC
    if (fullTopic == "Control/time")
        return syncTime(payload);
#endif

#if NM_ENABLE_CONSOLE
    String relative;
    const bool addressedHere = gDeviceIdentity.relativeTopic(fullTopic, relative);
    if (fullTopic == "all/console/in" || (addressedHere && relative == "console/in"))
    {
        runCommand(payload, gDeviceIdentity.topic("console/out"));
        return true;
    }
    if (addressedHere && relative.startsWith("console/controlled/") &&
        relative.endsWith("/in"))
    {
        const String id = relative.substring(sizeof("console/controlled/") - 1,
                                             relative.length() - sizeof("/in") + 1);
        if (!validControlId(id))
            return true;
        String reply = "console/controlled/";
        reply += id;
        reply += "/out";
        runCommand(payload, gDeviceIdentity.topic(reply));
        return true;
    }
#endif
    return false;
}
}
#endif // NM_ENABLE_MQTT
