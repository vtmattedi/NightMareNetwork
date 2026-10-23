#include <NightMare/Features.h>
#if NM_ENABLE_NETWORK
#include "NmMessageRouter.h"
#include "NmConnection.h"
#include "NmConnectionInternal.h"
#include "GatewayCandidate.h"

#include <Core/DeviceIdentity.h>
#include <Core/Logs.h>
#include <Core/ResourcesManager.h>
#include <Core/SystemState.h>
#if NM_ENABLE_TELEMETRY
#include <Core/Telemetry.h>
#endif
#if NM_ENABLE_TIME_SYNC
#include <Core/Time.h>
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
bool validControlId(const std::string &id)
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

// Console topics reach this router from whichever connection is active, so
// the source says which one -- not always MQTT.
CommandSource activeSource()
{
    return NightMare::GetActiveConnection() == NightMare::ConnectionType::ESP_NOW
               ? NM_CMD_SRC_ESPNOW
               : NM_CMD_SRC_MQTT;
}

void runCommand(const std::string &payload, const std::string &replyTopic)
{
    NightmareContext context(activeSource(), replyTopic);
    NightMareResults result = handleNightMareCommand(payload, context);
    if (result.context.msgSource != NM_CMD_ANS_DO_NOT_RESPOND)
        NightMare::PublishText(replyTopic, result.response, false);
}

#endif

#if NM_ENABLE_TIME_SYNC
bool syncTime(const std::string &payload)
{
    if (payload.length() > 256)
        return true;
    JsonDocument doc;
    if (deserializeJson(doc, payload) || doc["timestamp"].isNull() || doc["offset"].isNull())
        return true;
    std::string timestampText;
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
// Current-identity work only. Cleaning up a previous identity is not tied to a
// connection event: see processPendingIdentityCleanup().
void onConnected()
{
    bool subscriptionsComplete = true;
#if NM_ENABLE_CONSOLE
    subscriptionsComplete = NightMare::Subscribe(
                                gDeviceIdentity.topic("console/in").c_str()) &&
                            subscriptionsComplete;
    subscriptionsComplete = NightMare::Subscribe(
                                gDeviceIdentity.topic("console/controlled/+/in").c_str()) &&
                            subscriptionsComplete;
    subscriptionsComplete = NightMare::Subscribe("all/console/in") &&
                            subscriptionsComplete;
#endif
#if NM_ENABLE_TIME_SYNC
    subscriptionsComplete = NightMare::Subscribe("Control/time") &&
                            subscriptionsComplete;
#endif
    if (!subscriptionsComplete)
        LOG_WARNING("NET", "Message-router subscriptions were not accepted");

    SystemState.request(SystemRequest::PublishStatus);
    SystemState.request(SystemRequest::PublishManifest);
    SystemState.request(SystemRequest::PublishConsumeManifest);
    SystemState.request(SystemRequest::PublishResourceStates);
#if NM_ENABLE_TELEMETRY
    SystemState.request(SystemRequest::PublishInfo);
    SystemState.request(SystemRequest::PublishHardware);
    SystemState.request(SystemRequest::PublishTelemetry);
#endif
#if NM_ENABLE_CONSOLE
    NightMare::PublishText(gDeviceIdentity.topic("console/out"), firstConnection ? "Booted" : "Connected");
#endif
#if NM_ENABLE_TIME_SYNC
    if (!NightMare::Time::valid())
        NightMare::PublishText("Control/request", "time", false);
#endif
#if NM_ENABLE_CONSOLE
    firstConnection = false;
#endif
}

bool handleMessage(const std::string &fullTopic, const std::string &payload, bool retained)
{
    if (NightMare::GatewayCandidateHandleMessage(fullTopic, payload, retained))
        return true;
    std::string relative;
    const bool addressedHere = gDeviceIdentity.relativeTopic(fullTopic, relative);
    // The resource manager accepts owned /set and /invoke requests, as well as
    // manifests, states and events from other devices. It sees full topics in
    // both cases. Only a request addressed to this device ends here: another
    // device's traffic is an observation, and an application watching the
    // network (a dashboard, an explorer) must still see it.
    if (gResourcesManager.handleIngressMessage(fullTopic, payload, retained) && addressedHere)
        return true;

#if NM_ENABLE_TIME_SYNC
    if (fullTopic == "Control/time")
        return syncTime(payload);
#endif

#if NM_ENABLE_CONSOLE
    if (fullTopic == "all/console/in" || (addressedHere && relative == "console/in"))
    {
        runCommand(payload, gDeviceIdentity.topic("console/out"));
        return true;
    }
    if (addressedHere && NightMare::Text::startsWith(relative, "console/controlled/") &&
        NightMare::Text::endsWith(relative, "/in"))
    {
        const std::string id = NightMare::Text::slice(relative, sizeof("console/controlled/") - 1,
                                             relative.length() - sizeof("/in") + 1);
        if (!validControlId(id))
            return true;
        std::string reply = "console/controlled/";
        reply += id;
        reply += "/out";
        runCommand(payload, gDeviceIdentity.topic(reply));
        return true;
    }
#endif
    return NightMare::DispatchApplicationMessage(
        fullTopic.c_str(), reinterpret_cast<const uint8_t *>(payload.c_str()),
        payload.length(), retained);
}
}
#endif // NM_ENABLE_NETWORK
