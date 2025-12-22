#include "bWIFI.h"
#ifdef COMPILE_WIFI_MODULE
#define COMPILE_SERIAL
#ifdef COMPILE_SERIAL
#define WIFI_TAGF(fmt, ...) Serial.printf("%s%s" fmt "\n", MILLIS_LOG, WIFI_TAG, ##__VA_ARGS__)
#else
#define WIFI_TAGF(fmt, ...)
#endif

static WiFiConnectedCallback wifiConnectedCallback = nullptr;
static TaskHandle_t WiFiTaskHandle = nullptr;
#define COMPILE_SERIAL
/// @brief Set a callback function to be called when WiFi is connected
/// @param callback The callback function to be set
/// The callback function should have the signature: bool callback(bool firstConnection)
void onWiFiConnected(WiFiConnectedCallback callback)
{
    wifiConnectedCallback = callback;
}

void wifiConnectedInternal(bool firstConnection)
{
#ifdef COMPILE_OTA
    initOTA();
#endif
#ifdef COMPILE_TIMESYNC
    bool syncres = autoSyncTime();
    WIFI_TAGF("Time sync result: %s", OK_LOG(syncres));
#endif
    if (wifiConnectedCallback)
    {
        wifiConnectedCallback(firstConnection);
    }
}

/// @brief Task to monitor WiFi connection status changes
/// @param pvParameters Pointer to parameters (expected to be a bool indicating if the task should delete itself after connecting)
void WiFi_Task(void *pvParameters)
{
    wl_status_t old_state = WL_DISCONNECTED;
    bool firstConnection = true;
    bool deleteAfterConnect = *(bool *)pvParameters;
    delete (bool *)pvParameters;
    while (true)
    {
        if (WiFi.status() != old_state)
        {
            if (WiFi.status() == WL_CONNECTED)
            {
                WIFI_TAGF("WiFi Connected. IP Address: %s", WiFi.localIP().toString().c_str());
                wifiConnectedInternal(firstConnection);
                firstConnection = false;
                if (deleteAfterConnect)
                {
                    WiFiTaskHandle = NULL;
                    vTaskDelete(NULL);
                    return;
                }
            }
            else
            {
                WIFI_TAGF("new status: %d", WiFi.status());
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
    WIFI_TAGF("<Sync> Connecting to WiFi SSID: %s", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(DEVICE_NAME);
    WiFi.begin(ssid, password);
    unsigned int start = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        WIFI_TAGF("Waiting for WiFi connection. Current status: %d", WiFi.status());
        if (waitCallback)
        {
            waitCallback(millis() - start);
        }
        if (timeoutMs > 0 && millis() - start >= (unsigned int)timeoutMs)
        {
            return false;
        }
    }
    wifiConnectedInternal(true);
    return true;
}

bool WiFi_ConnectAsync(const char *ssid, const char *password, bool deleteAfterConnect)
{

    WIFI_TAGF("<Async> Connecting to WiFi SSID: %s", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(DEVICE_NAME);
    WiFi.begin(ssid, password);

    if (WiFiTaskHandle)
    {
#ifdef COMPILE_SERIAL
        Serial.printf("%s%s: WiFi Task is already running\n", ERR_TAG, WIFI_TAG);
#endif
        return false;
    }
    bool *deleteParam = new bool(deleteAfterConnect);
    bool res = xTaskCreatePinnedToCore(WiFi_Task,
                                       "WiFi_Task",
                                       4096,
                                       deleteParam,
                                       1,
                                       &WiFiTaskHandle,
                                       1);

#ifdef COMPILE_SERIAL
    if (res != pdPASS)
    {
        Serial.printf("%s%s: Failed to create WiFi_Task\n", ERR_TAG, WIFI_TAG);
    }
#endif
    return res;
}

/// @brief Disconnects from the WiFi network
void WiFi_Disconnect()
{
    WiFi.disconnect();
}

#ifdef COMPILE_CONFIGS

void WiFi_Auto()
{
    // Ensure Configs module is initialized
    Config.begin();
    if (!Config.exists("_ssid") || !Config.exists("_password"))
    {
        WIFI_TAGF("No stored WiFi credentials found. Using default.");
        Config.set("_ssid", DEFAULT_SSID, true);
        Config.set("_password", DEFAULT_PASSWORD, true);
    }
    String ssid = Config.get("_ssid");
    String password = Config.get("_password");
    WiFi_ConnectAsync(ssid.c_str(), password.c_str(), true);
}

bool WiFi_ChangeCredentials(const String &ssid, const String &password)
{
    // Ensure Configs module is initialized
    WiFi_Disconnect();
    bool result = WiFi_Connect(ssid.c_str(), password.c_str(), 15000);
    if (!result)
    {
        WIFI_TAGF("Failed to connect with new credentials. Keeping old ones.");
        WIFI_TAGF("Reconnecting to previous WiFi credentials.");
        String old_ssid = Config.get("_ssid");
        String old_password = Config.get("_password");
        WiFi_ConnectAsync(old_ssid.c_str(), old_password.c_str(), true);
        return false;
    }
    Config.set("_ssid", ssid, true);
    Config.set("_password", password, true);
    Config.save();
    WIFI_TAGF("WiFi credentials saved");
    return true;
}

#endif

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

#endif