#include <NightMare/Features.h>
#if NM_ENABLE_WIFI
#include "NightMareWIFI.h"
#include <Core/DeviceIdentity.h>
#include <Core/PersistentKeys.h>
#if NM_ENABLE_MQTT
#include <Network/MQTT.h>
#endif

static WiFiConnectedCallback wifiConnectedCallback = nullptr;
static TaskHandle_t WiFiTaskHandle = nullptr;
static bool firstConnection = true;
static int gTxPower = NightMare::NM_TX_POWER_AUTO;

// Every level wifi_power_t defines, in quarter-dBm.
static const int8_t kTxPowerLevels[] = {78, 76, 74, 68, 60, 52, 44, 34, 28, 20, 8, -4};

bool WiFi_isValidTxPower(int quarterDbm)
{
    if (quarterDbm == NightMare::NM_TX_POWER_AUTO)
        return true;
    for (int8_t level : kTxPowerLevels)
        if (level == quarterDbm)
            return true;
    return false;
}

/// Applies the configured tx power. AUTO leaves the driver alone; an invalid
/// value or a driver refusal is a failure, never a silent substitution.
static bool applyTxPower(int quarterDbm)
{
    if (quarterDbm == NightMare::NM_TX_POWER_AUTO)
        return true;
    if (!WiFi_isValidTxPower(quarterDbm))
        return false;
    return WiFi.setTxPower(static_cast<wifi_power_t>(quarterDbm));
}

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
#if NM_ENABLE_OTA
        initOTA();
#endif
#if NM_ENABLE_MQTT
        MQTT_Init(false);
#endif
#if NM_ENABLE_TIME_SYNC
        if (!startSntpTimeSync())
            LOG_ERROR("Time", "Could not start SNTP synchronization");
#endif
    }
    LOG("WiFi", "WiFi connected to SSID: %s, IP: %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
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
                    LOG("WiFi", "WiFi connected to SSID: %s, IP: %s", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
                    WiFiTaskHandle = NULL;
                    vTaskDelete(NULL);
                    return;
                }
            }
            old_state = WiFi.status();
        }
        int delayTime = old_state == WL_CONNECTED ? 5000 : 100;
        // LOG("WiFi", "WiFi status: %s", WiFi_getStatusName(old_state));
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
    WiFi.disconnect(true, true); // disconnect and erase old credentials
    WiFi.mode(WIFI_STA);
    if (!applyTxPower(gTxPower))
    {
        LOG_ERROR("WiFi", "Could not apply tx power %d", gTxPower);
        return false;
    }
    WiFi.setHostname(gDeviceIdentity.getDeviceName().c_str());
    gDeviceIdentity.lockAddress();
    // // A prior scanNetworks() (or a previous failed connect) can leave the
    // // driver's status stuck on a stale value; disconnect first so begin()
    // // actually starts a fresh association instead of being ignored.
    // WiFi.disconnect();
    WiFi.begin(ssid, password);
    unsigned int start = millis();
    LOG("WiFi", "Connecting to WiFi: %s", ssid);
    wl_status_t lastStatus = WiFi.status();
    while (WiFi.status() != WL_CONNECTED)
    {
        if (waitCallback)
        {
            waitCallback(millis() - start);
        }
        // if (WiFi.status() != lastStatus)
        // {
        //     lastStatus = WiFi.status();
        //     LOG("WiFi", "WiFi status: %s", WiFi_getStatusName(lastStatus));
        // }
        if (millis() % 200 == 0)
        {
            Serial.print(".");
            vTaskDelay(1 / portTICK_PERIOD_MS);
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
    WiFi.disconnect(true, true); // disconnect and erase old credentials
    WiFi.mode(WIFI_STA);
    if (!applyTxPower(gTxPower))
    {
        LOG_ERROR("WiFi", "Could not apply tx power %d", gTxPower);
        return false;
    }
    WiFi.setHostname(gDeviceIdentity.getDeviceName().c_str());
    // See WiFi_Connect: clears any stale status left by a prior scan/connect
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
    LOG("WiFi", "%s TASK: Created WiFi task for SSID: %s", OK_LOG(res), ssid);

    return res;
}

/// @brief Disconnects from the WiFi network
void WiFi_Disconnect()
{
    WiFi.disconnect();
}

NightMare::WiFiProfile WiFi_getProfile()
{
    // Ensure StateStore module is initialized
    PersistentSettings.begin();
    NightMare::WiFiProfile profile;
    profile.ssid = PersistentSettings.getOrSave(NightMare::PersistentKey::WifiSsid, DEFAULT_SSID);
    profile.password = PersistentSettings.getOrSave(NightMare::PersistentKey::WifiPassword, DEFAULT_PASSWORD);
    profile.txPower = PersistentSettings.getOrSave(NightMare::PersistentKey::WifiTxPower, String(NightMare::NM_TX_POWER_AUTO)).toInt();
    return profile;
}

bool WiFi_Auto()
{
    NightMare::WiFiProfile profile = WiFi_getProfile();
    gTxPower = profile.txPower;
    return WiFi_ConnectAsync(profile.ssid.c_str(), profile.password.c_str(), true);
}

bool WiFi_setTxPower(int quarterDbm)
{
    NightMare::WiFiProfile profile = WiFi_getProfile();
    profile.txPower = quarterDbm;
    return WiFi_changeProfile(profile);
}

float WiFi_getTxPowerDbm()
{
    if (WiFi.getMode() == WIFI_OFF)
        return NightMare::NM_TX_POWER_AUTO;
    return WiFi.getTxPower() / 4.0f;
}

void WiFi_Scan()
{
    LOG("WiFi", "Scanning for WiFi networks...");
    int n = WiFi.scanNetworks();
    LOG("WiFi", "Found %d networks", n);
    for (int i = 0; i < n; ++i)
    {
        LOG("WiFi", "%d: %s (%d) %s", i + 1, WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "Open" : "Secured");
    }
    // Releases the scan-result buffer and clears the driver's internal scan
    // state. Without this, a WiFi.begin() issued right after a scan can be
    // dropped or ignored: the driver's status flag is left over from the
    // scan and a fresh connect attempt is never actually started.
    WiFi.scanDelete();
}

bool WiFi_changeProfile(const NightMare::WiFiProfile &profile)
{
    if (!WiFi_isValidTxPower(profile.txPower))
        return false;
    NightMare::WiFiProfile old = WiFi_getProfile();
    WiFi_Disconnect();
    gTxPower = profile.txPower;
    if (!WiFi_Connect(profile.ssid.c_str(), profile.password.c_str(), 15000))
    {
        gTxPower = old.txPower;
        WiFi_ConnectAsync(old.ssid.c_str(), old.password.c_str(), true);
        return false;
    }
    // One write for the whole profile: a partial one would pair a new network
    // with the old power, or the reverse.
    const String keys[] = {NightMare::PersistentKey::WifiSsid, NightMare::PersistentKey::WifiPassword,
                           NightMare::PersistentKey::WifiTxPower};
    const String values[] = {profile.ssid, profile.password, String(profile.txPower)};
    return PersistentSettings.setMany(keys, values, 3);
}

bool WiFi_ChangeCredentials(const String &ssid, const String &password)
{
    NightMare::WiFiProfile profile;
    profile.ssid = ssid;
    profile.password = password;
    profile.txPower = gTxPower;
    return WiFi_changeProfile(profile);
}

const char *WiFi_getStatusName(wl_status_t status)
{
    switch (status)
    {
    case WL_NO_SHIELD:
        return "No Shield";
    case WL_IDLE_STATUS:
        return "Idle";
    case WL_NO_SSID_AVAIL:
        return "No SSID Available";
    case WL_SCAN_COMPLETED:
        return "Scan Completed";
    case WL_CONNECTED:
        return "Connected";
    case WL_CONNECT_FAILED:
        return "Connect Failed";
    case WL_CONNECTION_LOST:
        return "Connection Lost";
    case WL_DISCONNECTED:
        return "Disconnected";
    default:
        return "Unknown Status";
    }
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
#endif // NM_ENABLE_WIFI
