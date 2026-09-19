#include "NightMareWIFI.h"

static WiFiConnectedCallback wifiConnectedCallback = nullptr;
static TaskHandle_t WiFiTaskHandle = nullptr;
static bool firstConnection = true;
/// @brief Set a callback function to be called when WiFi is connected
/// @param callback The callback function to be set
/// The callback function should have the signature: bool callback(bool firstConnection)
void WiFi_onConnected(WiFiConnectedCallback callback)
{
    wifiConnectedCallback = callback;
}

void wifiConnectedInternal()
{
    if (firstConnection)
    {
#ifdef COMPILE_OTA
        initOTA();
#endif
#ifdef COMPILE_MQTT
        MQTT_Init(false);
#endif
#ifdef COMPILE_TIMESYNC
        autoSyncTime();
#endif
    }
    if (wifiConnectedCallback)
    {
        wifiConnectedCallback(firstConnection);
    }
    if (firstConnection)
    {
        firstConnection = false;
    }
}

/// @brief Task to monitor WiFi connection status changes
/// @param pvParameters Pointer to parameters (expected to be a bool indicating if the task should delete itself after connecting)
void WiFi_Task(void *pvParameters)
{
    wl_status_t old_state = WL_DISCONNECTED;
    bool deleteAfterConnect = *(bool *)pvParameters;
    delete (bool *)pvParameters;
    while (true)
    {
        if (WiFi.status() != old_state)
        {
            if (WiFi.status() == WL_CONNECTED)
            {
                wifiConnectedInternal();
                if (deleteAfterConnect)
                {
                    WiFiTaskHandle = NULL;
                    vTaskDelete(NULL);
                    return;
                }
            }
            old_state = WiFi.status();
        }
        int delayTime = old_state == WL_CONNECTED ? 5000 : 100;
        vTaskDelay(delayTime / portTICK_PERIOD_MS);
    }
}

/// @brief Connects to a WiFi network
/// @param ssid The SSID of the WiFi network
/// @param password The password of the WiFi network
/// @param timeoutMs The timeout in milliseconds for the connection attempt
/// Negative or zero timeout means wait indefinitely
/// @param waitCallback Optional callback function to be called periodically while waiting for connection
/// The callback function should have the signature: void callback(unsigned int elapsedTimeMs)
/// @return true if connected successfully, false otherwise
bool WiFi_Connect(const char *ssid, const char *password, int timeoutMs, void *waitCallback(unsigned int))
{
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(getDeviceName());
    WiFi.begin(ssid, password);
    unsigned int start = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        if (waitCallback)
        {
            waitCallback(millis() - start);
        }
        if (timeoutMs > 0 && millis() - start >= (unsigned int)timeoutMs)
        {
            return false;
        }
    }
    wifiConnectedInternal();
    return true;
}

bool WiFi_ConnectAsync(const char *ssid, const char *password, bool deleteAfterConnect)
{

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(getDeviceName());
    WiFi.begin(ssid, password);

    if (WiFiTaskHandle)
    {
        return false;
    }
    bool *deleteParam = new bool(deleteAfterConnect);
    // tskNO_AFFINITY instead of core 1: the ESP32-C6 (and C3/H2/S2) is
    // single-core, so pinning to core 1 fails configASSERT and panics.
    bool res = xTaskCreatePinnedToCore(WiFi_Task,
                                       "WiFi_Task",
                                       4096,
                                       deleteParam,
                                       1,
                                       &WiFiTaskHandle,
                                       tskNO_AFFINITY);

    return res;
}

/// @brief Disconnects from the WiFi network
void WiFi_Disconnect()
{
    WiFi.disconnect();
}

void WiFi_Auto()
{
    // Ensure StateStore module is initialized
    PersistentSettings.begin();
    if (!PersistentSettings.exists("_ssid") || !PersistentSettings.exists("_password"))
    {
        PersistentSettings.set("_ssid", DEFAULT_SSID);
        PersistentSettings.set("_password", DEFAULT_PASSWORD);
    }
    String ssid = PersistentSettings.get("_ssid");
    String password = PersistentSettings.get("_password");
    WiFi_ConnectAsync(ssid.c_str(), password.c_str(), true);
}

bool WiFi_ChangeCredentials(const String &ssid, const String &password)
{
    // Ensure StateStore module is initialized
    WiFi_Disconnect();
    bool result = WiFi_Connect(ssid.c_str(), password.c_str(), 15000);
    if (!result)
    {
        String old_ssid = PersistentSettings.get("_ssid");
        String old_password = PersistentSettings.get("_password");
        WiFi_ConnectAsync(old_ssid.c_str(), old_password.c_str(), true);
        return false;
    }
    PersistentSettings.set("_ssid", ssid);
    PersistentSettings.set("_password", password);
    PersistentSettings.save();
    return true;
}

const char *WiFi_getAuthTypeName(wifi_auth_mode_t authType)
{
    switch (authType)
    {
    case WIFI_AUTH_OPEN:
        return "OPEN";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA_PSK";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2_PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA_WPA2_PSK";
    case WIFI_AUTH_ENTERPRISE:
        return "ENTERPRISE";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3_PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2_WPA3_PSK";
    case WIFI_AUTH_WAPI_PSK:
        return "WAPI_PSK";
    case WIFI_AUTH_WPA3_ENT_192:
        return "WPA3_ENT_192";
    default:
        return "UNKNOWN";
    }
}

