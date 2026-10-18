#include <Gateway/GatewayCommands.h>
#if NM_ENABLE_GATEWAY && NM_ENABLE_CONSOLE
#include <Gateway/NightMareGateway.h>
#include <Gateway/GatewayState.h>
#include <Gateway/HardwareTrace.h>
#if NM_GATEWAY_ESPNOW
#include <Gateway/EspNow/espBroker.h>
#endif
#if NM_GATEWAY_MQTT
#include <Gateway/EspMqtt/espMqtt.h>
#endif
#include <Core/Text.h>
#include <ArduinoJson.h>
#include "esp_timer.h"
#include <cstdlib>

namespace
{
constexpr char Usage[] =
    "GATEWAY GET | ENABLE | DISABLE | CLIENTS | ESPNOW GET|PAUSE|RESUME | TRACE GET|ENABLE|DISABLE"
    " | MQTT GET | MQTT TEST [bytes [count]] | MQTT LINKDEBUG GET|ENABLE|DISABLE";

NightMareResults answer(const JsonDocument &doc, bool ok, NightmareContext context)
{
    NightMareResults result;
    result.result = ok;
    result.context = context;
    serializeJson(doc, result.response);
    return result;
}

NightMareResults refuse(const char *reason, NightmareContext context, const char *usage = nullptr)
{
    JsonDocument doc;
    doc["ok"] = false;
    doc["reason"] = reason;
    if (usage != nullptr)
        doc["usage"] = usage;
    return answer(doc, false, context);
}

NightMareResults accepted(NightmareContext context)
{
    JsonDocument doc;
    doc["ok"] = true;
    return answer(doc, true, context);
}

std::string word(const NightMareMessage &message, uint8_t index)
{
    std::string value = message.args[index];
    NightMare::Text::toUpper(value);
    return value;
}

// Plain decimal, nothing trailing.
bool parseCount(const std::string &text, uint32_t &value)
{
    if (text.empty() || text.size() > 10)
        return false;
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || text[0] == '-' || parsed > UINT32_MAX)
        return false;
    value = static_cast<uint32_t>(parsed);
    return true;
}

bool espNowUp()
{
#if NM_GATEWAY_ESPNOW
    return espBroker_subscriberCount() > 0;
#else
    return false;
#endif
}

bool mqttUp()
{
#if NM_GATEWAY_MQTT
    return mqtt_is_connected();
#else
    return false;
#endif
}

const char *linkName()
{
    const bool local = espNowUp();
    const bool remote = mqttUp();
    return local && remote ? "BRIDGED" : remote ? "REMOTE" : local ? "LOCAL" : "NONE";
}

void addEspNow(JsonObject espnow)
{
#if NM_GATEWAY_ESPNOW
    espnow["supported"] = true;
    espnow["paused"] = NightMareGateway_espNowPaused();
    espnow["beacon"] = espBroker_beaconActive();
    const uint32_t sinceBeacon = espBroker_secondsSinceLastBeacon();
    if (sinceBeacon == UINT32_MAX)
        espnow["since_beacon_s"] = nullptr;
    else
        espnow["since_beacon_s"] = sinceBeacon;
    espnow["sessions"] = espBroker_deviceCount();
    espnow["subscribers"] = espBroker_subscriberCount();
#else
    espnow["supported"] = false;
#endif
}

void addMqtt(JsonObject mqtt)
{
#if NM_GATEWAY_MQTT
    mqtt["supported"] = true;
    mqtt["connected"] = mqtt_is_connected();
    mqtt["linkdebug"] = mqtt_wifiDumpOnDisconnect();
#else
    mqtt["supported"] = false;
#endif
}

NightMareResults status(NightmareContext context)
{
    JsonDocument doc;
    doc["enabled"] = NightMareGateway_enabled();
    doc["state"] = NightMareGateway_stateName(NightMareGateway_state());
    doc["link"] = linkName();
    doc["id"] = GatewayState::id();
    doc["generation"] = GatewayState::generation();
    addEspNow(doc["espnow"].to<JsonObject>());
    addMqtt(doc["mqtt"].to<JsonObject>());
    JsonObject vault = doc["vault"].to<JsonObject>();
    vault["count"] = NightMareGateway_vaultSize();
    vault["version"] = NightMareGateway_vaultVersion();
    doc["trace"] = NightMareGateway_hardwareTrace();
    return answer(doc, true, context);
}

NightMareResults clients(NightmareContext context)
{
#if NM_GATEWAY_ESPNOW
    std::vector<EspBrokerDeviceInfo> devices;
    espBroker_snapshotDevices(devices);
    const uint64_t nowMs = static_cast<uint64_t>(esp_timer_get_time() / 1000);

    JsonDocument doc;
    JsonArray list = doc["clients"].to<JsonArray>();
    for (const EspBrokerDeviceInfo &device : devices)
    {
        JsonObject entry = list.add<JsonObject>();
        entry["mac"] = device.mac;
        entry["name"] = device.name;
        entry["cid"] = device.cid;
        entry["state"] = NightMare::connectionStateName(device.state);
        entry["suspended"] = device.suspended;
        entry["subscriptions"] = device.subscriptions.size();
        if (device.hasRssi)
            entry["rssi"] = device.avgRssi;
        else
            entry["rssi"] = nullptr;
        if (device.hasRtt)
            entry["rtt_ms"] = device.rttMs;
        else
            entry["rtt_ms"] = nullptr;
        entry["last_seen_s"] = (nowMs - device.lastSeenMs) / 1000;
        entry["last_will"] = device.hasLastWill;
        entry["disconnect_candidate"] = device.disconnectCandidate;
    }
    return answer(doc, true, context);
#else
    return refuse("unsupported", context);
#endif
}

NightMareResults espNow(const NightMareMessage &message, NightmareContext context)
{
#if NM_GATEWAY_ESPNOW
    const std::string action = word(message, 1);
    if (action.empty() || action == "GET")
    {
        JsonDocument doc;
        addEspNow(doc.to<JsonObject>());
        return answer(doc, true, context);
    }
    if (action == "PAUSE" || action == "RESUME")
    {
        NightMareGateway_pauseEspNow(action == "PAUSE");
        return accepted(context);
    }
    return refuse("unknown_espnow_command", context, "GATEWAY ESPNOW GET|PAUSE|RESUME");
#else
    (void)message;
    return refuse("unsupported", context);
#endif
}

NightMareResults trace(const NightMareMessage &message, NightmareContext context)
{
    const std::string action = word(message, 1);
    if (action == "ENABLE" || action == "DISABLE")
        NightMareGateway_setHardwareTrace(action == "ENABLE");
    else if (!action.empty() && action != "GET")
        return refuse("unknown_trace_command", context, "GATEWAY TRACE GET|ENABLE|DISABLE");
    JsonDocument doc;
    doc["enabled"] = NightMareGateway_hardwareTrace();
    return answer(doc, true, context);
}

NightMareResults mqtt(const NightMareMessage &message, NightmareContext context)
{
#if NM_GATEWAY_MQTT
    const std::string action = word(message, 1);
    if (action.empty() || action == "GET")
    {
        JsonDocument doc;
        addMqtt(doc.to<JsonObject>());
        return answer(doc, true, context);
    }
    if (action == "TEST")
    {
        uint32_t bytes = 1024;
        uint32_t count = 10;
        if ((message.argc > 2 && !parseCount(message.args[2], bytes)) ||
            (message.argc > 3 && !parseCount(message.args[3], count)) ||
            bytes == 0 || bytes > 32768 || count == 0 || count > 200)
            return refuse("invalid_arguments", context, "GATEWAY MQTT TEST [bytes 1..32768 [count 1..200]]");
        if (!mqtt_startDiagTest(bytes, count))
            return refuse("not_connected_or_running", context);
        JsonDocument doc;
        doc["ok"] = true;
        doc["log"] = "mqtttest"; // results arrive on this log module
        return answer(doc, true, context);
    }
    if (action == "LINKDEBUG")
    {
        const std::string mode = word(message, 2);
        if (mode == "ENABLE" || mode == "DISABLE")
            mqtt_setWifiDumpOnDisconnect(mode == "ENABLE");
        else if (!mode.empty() && mode != "GET")
            return refuse("unknown_linkdebug_command", context, "GATEWAY MQTT LINKDEBUG GET|ENABLE|DISABLE");
        JsonDocument doc;
        doc["enabled"] = mqtt_wifiDumpOnDisconnect();
        return answer(doc, true, context);
    }
    return refuse("unknown_mqtt_command", context, "GATEWAY MQTT GET | TEST [bytes [count]] | LINKDEBUG GET|ENABLE|DISABLE");
#else
    (void)message;
    return refuse("unsupported", context);
#endif
}
}

NightMareResults executeGatewayCommand(const NightMareMessage &message, NightmareContext context)
{
    const std::string &verb = message.subcommand;
    if (verb.empty() || verb == "GET")
        return status(context);
    if (verb == "ENABLE" || verb == "DISABLE")
    {
        NightMareGateway_enable(verb == "ENABLE");
        return accepted(context);
    }
    if (verb == "CLIENTS")
        return clients(context);
    if (verb == "ESPNOW")
        return espNow(message, context);
    if (verb == "TRACE")
        return trace(message, context);
    if (verb == "MQTT")
        return mqtt(message, context);
    return refuse("unknown_gateway_command", context, Usage);
}

#endif // NM_ENABLE_GATEWAY && NM_ENABLE_CONSOLE
