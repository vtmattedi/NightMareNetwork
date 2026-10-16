#include <NightMare/Features.h>
#if NM_ENABLE_NETWORK

#include "GatewayCandidate.h"

#include <ArduinoJson.h>
#include <Core/Logs.h>
#if NM_ENABLE_WIFI
#include <Network/WiFiIP/NmWifiEsp.h>
#endif
#include <Network/NmConnectionInternal.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace NightMare
{
namespace
{
constexpr const char *NetworkSuffix = "/gateway/network";
constexpr const char *StatusSuffix = "/status";
GatewayCandidateStatus current;
SemaphoreHandle_t lock = nullptr;

bool ensureLock()
{
    if (lock == nullptr)
        lock = xSemaphoreCreateMutex();
    return lock != nullptr;
}

bool topicGatewayId(const std::string &topic, std::string &id)
{
    if (!NightMare::Text::endsWith(topic, NetworkSuffix) || topic.length() <= strlen(NetworkSuffix))
        return false;
    id = NightMare::Text::slice(topic, 0, topic.length() - strlen(NetworkSuffix));
    return NightMare::Text::indexOf(id, '/') < 0 && id.length() <= 64;
}
}

bool GatewayCandidateProbable(const GatewayCandidateStatus &candidate,
                              MqttProfile requiredUplink,
                              const GatewayRadioContext &radio)
{
    if (!candidate.known || candidate.id.empty() || !candidate.espNowReady)
        return false;
    if (requiredUplink == MqttProfile::REMOTE && !candidate.remoteMqttReady)
        return false;
    if (requiredUplink == MqttProfile::LOCAL && !candidate.localMqttReady)
        return false;
    if (candidate.channel == 0 || radio.channel == 0 || candidate.channel != radio.channel)
        return false;
    if (!candidate.bssid.empty() && !radio.bssid.empty())
        return NightMare::Text::equalsIgnoreCase(candidate.bssid, radio.bssid);
    return !candidate.ssid.empty() && candidate.ssid == radio.ssid;
}

bool GatewayCandidateHandleMessage(const std::string &topic, const std::string &payload, bool retained)
{
    std::string topicId;
    if (!retained)
        return false;
    const bool network = topicGatewayId(topic, topicId);
    if (!network)
    {
        if (!NightMare::Text::endsWith(topic, StatusSuffix) || topic.length() <= strlen(StatusSuffix))
            return false;
        topicId = NightMare::Text::slice(topic, 0, topic.length() - strlen(StatusSuffix));
        if (NightMare::Text::indexOf(topicId, '/') >= 0)
            return false;
    }
    JsonDocument doc;
    if (deserializeJson(doc, payload) || doc["schema"].as<int>() != 1)
        return false;
    const std::string id = doc["id"].as<std::string>();
    const std::string generation = doc["generation"].as<std::string>();
    if (id != topicId || id.empty() || generation.empty())
        return false;
    if (!network)
    {
        if (doc["kind"].as<std::string>() != "gateway")
            return false;
        if (!ensureLock())
            return false;
        xSemaphoreTake(lock, portMAX_DELAY);
        if (current.id == id && current.generation == generation && !(doc["online"] | false))
            current.known = false;
        xSemaphoreGive(lock);
        OnConnectivityStateChanged();
        return true;
    }

    GatewayCandidateStatus next;
    next.known = doc["online"] | false;
    next.id = id;
    next.generation = generation;
    next.espNowReady = doc["capabilities"]["esp_now"] | false;
    next.remoteMqttReady = doc["uplinks"]["remote_mqtt"]["ready"] | false;
    next.localMqttReady = doc["uplinks"]["local_mqtt"]["ready"] | false;
    next.ssid = doc["radio"]["ssid"].as<std::string>();
    next.bssid = doc["radio"]["bssid"].as<std::string>();
    next.channel = doc["radio"]["channel"] | 0;
    if (!ensureLock())
        return false;
    xSemaphoreTake(lock, portMAX_DELAY);
    current = next;
    xSemaphoreGive(lock);
    OnConnectivityStateChanged();
    return true;
}

GatewayCandidateStatus GatewayCandidateGet()
{
    if (!ensureLock())
        return GatewayCandidateStatus();
    xSemaphoreTake(lock, portMAX_DELAY);
    const GatewayCandidateStatus copy = current;
    xSemaphoreGive(lock);
    return copy;
}

bool GatewayCandidateIsProbable(MqttProfile requiredUplink)
{
    GatewayRadioContext radio;
#if NM_ENABLE_WIFI
    const WiFiInfo wifi = WiFi_info();
    radio.ssid = wifi.ssid.c_str();
    radio.bssid = wifi.bssid.c_str();
    radio.channel = wifi.channel;
#endif
    return GatewayCandidateProbable(GatewayCandidateGet(), requiredUplink, radio);
}

bool GatewayCandidateMatchesAuthenticated(const char *gatewayId)
{
    const GatewayCandidateStatus candidate = GatewayCandidateGet();
    return candidate.known && gatewayId != nullptr && candidate.id == gatewayId;
}

void GatewayCandidateOnConnected()
{
    bool complete = Subscribe(GatewayNetworkTopicFilter());
    complete = Subscribe(GatewayStatusTopicFilter()) && complete;
    if (!complete)
        LOG_WARNING("NET", "Gateway candidate subscriptions were not accepted");
}

const char *GatewayNetworkTopicFilter() { return "+/gateway/network"; }
const char *GatewayStatusTopicFilter() { return "+/status"; }
}

#endif
