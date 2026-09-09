#include <Core/SystemStatus.h>
#ifdef COMPILE_SYSTEMSTATUS

// Every dependency the report aggregates lives here rather than in the header. A .cpp is never
// included by anything, so no matter how many modules this reaches for, it cannot create a cycle.
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Core/Configs.h> // SystemSettings
#include <Core/Misc.h>    // ramUsagePercent
#ifdef COMPILE_MQTT
#include <Core/MQTT.h>
#endif
#ifdef COMPILE_HTTP_SERVER
#include <HTTP/http.h>
#endif
#ifdef COMPILE_ASYNC_COMMANDS
#include <Xtra/NightMareAsyncCommands.h>
#endif

/// @brief Standardized system status report in JSON format.
/// @return A String containing the system status in JSON format.
String getSystemStatus()
{
    DynamicJsonDocument doc(1024);
#ifdef COMPILE_HTTP_SERVER
    bool httpDirect = getHttpState() > 0;
#else
    bool httpDirect = false; // TODO: implement direct http and set this to true when it's implemented and enabled.
#endif

    JsonObject system = doc.createNestedObject("System");
    system["Uptime"] = millis() / 1000;
    system["FreeHeap"] = ramUsagePercent();
    system["boot_time"] = SystemSettings.get("boot_time");
    system["time_synced"] = SystemSettings.getFlag("time_synced");
    system["reset_reason"] = esp_reset_reason();
    system["wifi_rssi"] = WiFi.RSSI();
#ifdef COMPILE_MQTT
    system["mqtt_connection"] = MQTT_isLocal() ? "Local" : "Remote";
#else
    system["mqtt_connection"] = "Disabled";
#endif
    system["ip_address"] = WiFi.localIP().toString();
    system["direct_http"] = httpDirect;
    system["OTA_enabled"] = SystemSettings.getFlag("ota_enabled");
#ifdef COMPILE_ASYNC_COMMANDS
    system["ASYNC_enabled"] = isAsyncCommandSystemReady();
#else
    system["ASYNC_enabled"] = false;
#endif
    String msg;
    serializeJson(doc, msg);
    return msg;
}

#endif
