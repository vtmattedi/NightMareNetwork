#include <NightMare/Features.h>
#if NM_ENABLE_TELEMETRY

#include "Telemetry.h"
#include "DeviceIdentity.h"
#include "Scheduler.h"
#include "StateStore.h"
#include "Time.h"
#include <NightMare/HardwareProfile.h>
#include <Network/MQTT.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#if NM_ENABLE_WIFI
#include <WiFi.h>
#endif

namespace
{
    constexpr const char *TelemetryJobLabel = "nm.telemetry";

    const char *directionName(NMHardware::Direction direction)
    {
        switch (direction)
        {
        case NMHardware::Direction::Input:
            return "input";
        case NMHardware::Direction::Output:
            return "output";
        case NMHardware::Direction::Bidirectional:
            return "bidirectional";
        case NMHardware::Direction::Power:
            return "power";
        case NMHardware::Direction::Ground:
            return "ground";
        case NMHardware::Direction::Bus:
            return "bus";
        }
        return "unknown";
    }

    const char *pullName(NMHardware::Pull pull)
    {
        switch (pull)
        {
        case NMHardware::Pull::None:
            return "none";
        case NMHardware::Pull::Up:
            return "up";
        case NMHardware::Pull::Down:
            return "down";
        case NMHardware::Pull::External:
            return "external";
        }
        return "unknown";
    }
}

TelemetryService Telemetry;
static int32_t heartbeat = 0;
bool TelemetryService::start(uint32_t intervalMs)
{
    if (intervalMs == 0)
        return false;
    if (started_ && intervalMs_ == intervalMs)
        return true;
    gScheduler.begin(); // Monotonic jobs work even if persistent storage is unavailable.
    if (started_)
        gScheduler.remove(TelemetryJobLabel);
    if (gScheduler.everyMonotonic(TelemetryJobLabel, "TELEMETRY PUBLISH", intervalMs) < 0)
        return false;
    gScheduler.timer("heartbeat", [](){ MQTT_Publish("/heartbeat", String(heartbeat++), true, false); }, 300 * 1000);
    started_ = true;
    intervalMs_ = intervalMs;
    return true;
}

String TelemetryService::snapshotJson() const
{
    const NMHardware::Profile profile = NMHardware::getProfile();
    if (profile.connectionCount > 128 ||
        (profile.connectionCount != 0 && profile.connections == nullptr))
        return String();

    DynamicJsonDocument doc(2048 + profile.connectionCount * 256);
    doc["version"] = 1;
    JsonObject identity = doc.createNestedObject("identity");
    identity["name"] = gDeviceIdentity.getDeviceName();
    identity["id"] = gDeviceIdentity.getDeviceId();

    JsonObject hardware = doc.createNestedObject("hardware");
    hardware["board"] = profile.boardName != nullptr ? profile.boardName : "unspecified";
    hardware["chip"] = ESP.getChipModel();
    hardware["cores"] = ESP.getChipCores();
    hardware["revision"] = ESP.getChipRevision();
    hardware["flash_bytes"] = ESP.getFlashChipSize();
    hardware["heap_bytes"] = ESP.getHeapSize();
    hardware["psram_bytes"] = ESP.getPsramSize();
    JsonArray connections = hardware.createNestedArray("connections");
    for (size_t i = 0; i < profile.connectionCount; ++i)
    {
        const NMHardware::Connection &connection = profile.connections[i];
        JsonObject pin = connections.createNestedObject();
        pin["name"] = connection.name != nullptr ? connection.name : "";
        pin["pin"] = connection.pin;
        pin["direction"] = directionName(connection.direction);
        pin["pull"] = pullName(connection.pull);
        pin["active_low"] = connection.activeLow;
        if (connection.note != nullptr && connection.note[0] != '\0')
            pin["note"] = connection.note;
    }

    JsonObject build = doc.createNestedObject("build");
    build["date"] = __DATE__;
    build["time"] = __TIME__;
    build["firmware_version"] = NM_FIRMWARE_VERSION;
    build["compiler"] = __VERSION__;
    build["arduino_version"] = ARDUINO;
    build["esp_idf_version"] = ESP.getSdkVersion();
    JsonObject features = build.createNestedObject("features");
    features["settings"] = NM_ENABLE_SETTINGS != 0;
    features["network"] = NM_ENABLE_NETWORK != 0;
    features["wifi"] = NM_ENABLE_WIFI != 0;
    features["mqtt"] = NM_ENABLE_MQTT != 0;
    features["console"] = NM_ENABLE_CONSOLE != 0;
    features["resources"] = NM_ENABLE_RESOURCES != 0;
    features["scheduler"] = NM_ENABLE_SCHEDULER != 0;
    features["jobs"] = NM_ENABLE_JOBS != 0;
    features["telemetry"] = NM_ENABLE_TELEMETRY != 0;
    features["time_sync"] = NM_ENABLE_TIME_SYNC != 0;
    features["ota"] = NM_ENABLE_OTA != 0;
    features["http"] = NM_ENABLE_HTTP != 0;
    features["websocket"] = NM_ENABLE_WEBSOCKET != 0;
    features["lvgl"] = NM_ENABLE_LVGL != 0;

    JsonObject boot = doc.createNestedObject("boot");
    boot["reset_reason"] = static_cast<int>(esp_reset_reason());
    boot["time_synced"] = NightMare::Time::valid();
    if (boot["time_synced"].as<bool>())
        boot["boot_epoch_seconds"] = NightMare::Time::now() - (millis() / 1000);

    JsonObject system = doc.createNestedObject("system");
    system["uptime_ms"] = millis();
    system["cpu_mhz"] = ESP.getCpuFreqMHz();
    system["free_heap_bytes"] = ESP.getFreeHeap();
    system["min_free_heap_bytes"] = ESP.getMinFreeHeap();
    system["free_psram_bytes"] = ESP.getFreePsram();
    system["mqtt_connected"] = MQTT_Connected();
#if NM_ENABLE_WIFI
    system["wifi_connected"] = WiFi.status() == WL_CONNECTED;
    if (WiFi.status() == WL_CONNECTED)
        system["wifi_rssi_dbm"] = WiFi.RSSI();
#endif

    if (doc.overflowed())
        return String();
    String output;
    serializeJson(doc, output);
    return output;
}

bool TelemetryService::publish()
{
    const String payload = snapshotJson();
    return payload.length() > 0 && MQTT_Publish("telemetry", payload, true, true);
}

#endif // NM_ENABLE_TELEMETRY
