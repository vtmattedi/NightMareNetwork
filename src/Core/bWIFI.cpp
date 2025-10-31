#include "bWIFI.h"
#ifdef COMPILE_WIFI_MODULE

#ifdef COMPILE_SERIAL
#define WIFI_LOGF(fmt, ...) Serial.printf("%s %s " fmt "\n", WIFI_LOG, MILLIS_LOG, ##__VA_ARGS__)
#else
#define WIFI_LOGF(fmt, ...)
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
                WIFI_LOGF("WiFi Connected. IP Address: %s", WiFi.localIP().toString().c_str());
                wifiConnectedInternal(firstConnection);
                firstConnection = false;
                if (deleteAfterConnect)
                {
                    WiFiTaskHandle = NULL;
                    vTaskDelete(NULL);
                }
            }
            else
            {
                WIFI_LOGF("new status: %d", WiFi.status());
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
void WiFi_Connect(const char *ssid, const char *password, int timeoutMs, void *waitCallback(unsigned int))
{
    WIFI_LOGF("<Sync> Connecting to WiFi SSID: %s", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(DEVICE_NAME);
    WiFi.begin(ssid, password);
    unsigned int start = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        WIFI_LOGF("Waiting for WiFi connection. Current status: %d", WiFi.status());
        if (waitCallback)
        {
            waitCallback(millis() - start);
        }
        if (timeoutMs > 0 && millis() - start >= (unsigned int)timeoutMs)
        {
            return;
        }
    }
    wifiConnectedInternal(true);
}

bool WiFi_ConnectAsync(const char *ssid, const char *password, bool deleteAfterConnect)
{

    WIFI_LOGF("<Async> Connecting to WiFi SSID: %s", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(DEVICE_NAME);
    WiFi.begin(ssid, password);

    if (WiFiTaskHandle)
    {
#ifdef COMPILE_SERIAL
        Serial.printf("%s%s: WiFi Task is already running\n", ERR_LOG, WIFI_LOG);
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
        Serial.printf("%s%s: Failed to create WiFi_Task\n", ERR_LOG, WIFI_LOG);
    }
#endif
    return res;
}

/// @brief Disconnects from the WiFi network
void WiFi_Disconnect()
{
    WiFi.disconnect();
}
#endif