#include <Gateway/NightMareGateway.h>
#if NM_ENABLE_GATEWAY
#include <Gateway/GatewayState.h>

#include <Gateway/EspNow/espBroker.h>
#if NM_GATEWAY_MQTT
#include <Gateway/EspMqtt/espMqtt.h>
#endif
#include "cJSON.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include <Core/DeviceIdentity.h>
#include <stdio.h>
#include <stdlib.h>

namespace GatewayState
{
namespace
{
std::string gatewayId;
std::string hardwareSignature;
std::string timezoneName; // as of boot: status is also built on the MQTT task
std::string bootGeneration;
bool remoteBridge = false; // setUplink()
bool localBridge = false;

std::string render(cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    std::string result = text == nullptr ? std::string() : std::string(text);
    cJSON_free(text);
    cJSON_Delete(root);
    return result;
}

// The gateway announcement fields NightMare's GatewayCandidate reads, plus the
// name/hardware/timezone a NightMare device's own status carries, so the same
// <id>/status reads as either.
void addIdentity(cJSON *root, bool online)
{
    cJSON_AddNumberToObject(root, "schema", 1);
    cJSON_AddStringToObject(root, "name", gatewayId.c_str());
    cJSON_AddStringToObject(root, "hardware", hardwareSignature.c_str());
    cJSON_AddStringToObject(root, "timezone", timezoneName.c_str());
    cJSON_AddStringToObject(root, "device", gatewayId.c_str());
    cJSON_AddStringToObject(root, "id", gatewayId.c_str());
    cJSON_AddStringToObject(root, "kind", "gateway");
    cJSON_AddBoolToObject(root, "online", online);
    cJSON_AddStringToObject(root, "generation", bootGeneration.c_str());
}
}

void begin()
{
    if (!gatewayId.empty())
        return;
    // The NightMare device name: the topic root, the MQTT client id and the id
    // in every beacon. Locked for this boot, so an adoption (ADOPT <name>)
    // takes effect after a reboot instead of moving a live id.
    gatewayId = gDeviceIdentity.getDeviceName();
    gDeviceIdentity.lockAddress();
    hardwareSignature = gDeviceIdentity.getHardwareSignature();
    timezoneName = gDeviceIdentity.getTimezone();
    char generationText[17];
    snprintf(generationText, sizeof(generationText), "%08lx%08lx",
             (unsigned long)esp_random(), (unsigned long)esp_random());
    bootGeneration = generationText;
}

void setUplink(bool enabled, bool remote)
{
    remoteBridge = enabled && remote;
    localBridge = enabled && !remote;
}

const std::string &id() { begin(); return gatewayId; }
const std::string &generation() { begin(); return bootGeneration; }
std::string statusTopic() { return id() + "/status"; }
std::string networkTopic() { return id() + "/gateway/network"; }
std::string clientsTopic() { return id() + "/gateway/clients"; }

std::string statusJson(bool online)
{
    cJSON *root = cJSON_CreateObject();
    addIdentity(root, online);
    return render(root);
}

std::string networkJson(bool online)
{
    cJSON *root = cJSON_CreateObject();
    addIdentity(root, online);
    cJSON *capabilities = cJSON_AddObjectToObject(root, "capabilities");
#if NM_GATEWAY_ESPNOW
    cJSON_AddBoolToObject(capabilities, "esp_now", espBroker_beaconActive());
#else
    cJSON_AddBoolToObject(capabilities, "esp_now", false);
#endif
    cJSON_AddBoolToObject(capabilities, "mqtt_bridge_remote", remoteBridge);
    cJSON_AddBoolToObject(capabilities, "mqtt_bridge_local", localBridge);

    wifi_ap_record_t ap = {};
    const bool associated = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    cJSON *radio = cJSON_AddObjectToObject(root, "radio");
    cJSON_AddNumberToObject(radio, "channel", associated ? ap.primary : 0);
    cJSON_AddStringToObject(radio, "ssid", associated ? reinterpret_cast<const char *>(ap.ssid) : "");
    char bssid[18] = {};
    if (associated)
        snprintf(bssid, sizeof(bssid), "%02x:%02x:%02x:%02x:%02x:%02x",
                 ap.bssid[0], ap.bssid[1], ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);
    cJSON_AddStringToObject(radio, "bssid", bssid);

#if NM_GATEWAY_MQTT
    const bool uplinkUp = mqtt_is_connected();
#else
    const bool uplinkUp = false;
#endif
    cJSON *uplinks = cJSON_AddObjectToObject(root, "uplinks");
    cJSON *remote = cJSON_AddObjectToObject(uplinks, "remote_mqtt");
    cJSON *local = cJSON_AddObjectToObject(uplinks, "local_mqtt");
    cJSON_AddBoolToObject(remote, "ready", online && remoteBridge && uplinkUp);
    cJSON_AddBoolToObject(local, "ready", online && localBridge && uplinkUp);
    return render(root);
}

std::string clientsJson(const std::vector<EspBrokerDeviceInfo> &devices)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schema", 1);
    cJSON_AddStringToObject(root, "id", id().c_str());
    cJSON_AddStringToObject(root, "generation", generation().c_str());
    cJSON *clients = cJSON_AddArrayToObject(root, "clients");
    for (const EspBrokerDeviceInfo &device : devices)
    {
        cJSON *entry = cJSON_CreateObject();
        cJSON_AddStringToObject(entry, "id", device.mac.c_str());
        cJSON_AddStringToObject(entry, "name", device.name.c_str());
        cJSON_AddBoolToObject(entry, "connected", device.state == NightMare::ConnectionState::CONNECTED && !device.suspended);
        cJSON *will = cJSON_AddObjectToObject(entry, "last_will");
        cJSON_AddStringToObject(will, "topic", device.lastWillTopic.c_str());
        cJSON_AddBoolToObject(will, "retained", device.lastWillRetained);
        cJSON *payload = cJSON_AddArrayToObject(will, "payload");
        for (uint8_t byte : device.lastWillPayload)
            cJSON_AddItemToArray(payload, cJSON_CreateNumber(byte));
        cJSON_AddItemToArray(clients, entry);
    }
    return render(root);
}
}

#endif // NM_ENABLE_GATEWAY
