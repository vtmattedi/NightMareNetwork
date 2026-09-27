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
static bool wifiTaskStaysActive = false;
static bool wifiTaskPersistsPower = false;
static bool firstConnection = true;
int gTxPower = NightMare::NM_TX_POWER_AUTO;

namespace
{
    constexpr uint32_t AsyncConnectionAttemptMs = 15000;

    struct AsyncWiFiParameters
    {
        String ssid;
        String password;
        bool deleteAfterConnect;
        bool persistSuccessfulPower;
        bool notifyInitialConnection;
        int configuredPower;
    };

    bool savePowerProfile(const NightMare::WiFiProfile &profile);
    bool createAsyncTask(const char *ssid, const char *password,
                         bool deleteAfterConnect, bool persistSuccessfulPower,
                         bool notifyInitialConnection);
    bool startAsyncConnection(const char *ssid, const char *password,
                              bool deleteAfterConnect, bool persistSuccessfulPower);
    bool startAsyncMonitor(const char *ssid, const char *password,
                           bool persistSuccessfulPower);
}

// typedef enum {
//   WIFI_POWER_21dBm = 84,      // 21dBm
//   WIFI_POWER_20_5dBm = 82,    // 20.5dBm
//   WIFI_POWER_20dBm = 80,      // 20dBm
//   WIFI_POWER_19_5dBm = 78,    // 19.5dBm
//   WIFI_POWER_19dBm = 76,      // 19dBm
//   WIFI_POWER_18_5dBm = 74,    // 18.5dBm
//   WIFI_POWER_17dBm = 68,      // 17dBm
//   WIFI_POWER_15dBm = 60,      // 15dBm
//   WIFI_POWER_13dBm = 52,      // 13dBm
//   WIFI_POWER_11dBm = 44,      // 11dBm
//   WIFI_POWER_8_5dBm = 34,     // 8.5dBm
//   WIFI_POWER_7dBm = 28,       // 7dBm
//   WIFI_POWER_5dBm = 20,       // 5dBm
//   WIFI_POWER_2dBm = 8,        // 2dBm
//   WIFI_POWER_MINUS_1dBm = -4  // -1dBm
// } wifi_power_t;

// Every level wifi_power_t defines, in quarter-dBm.
static const int8_t kTxPowerLevels[] = {84, 82, 80, 78, 76, 74, 68, 60, 52, 44, 34, 28, 20, 8, -4};
static constexpr size_t kTxPowerLevelCount = sizeof(kTxPowerLevels) / sizeof(kTxPowerLevels[0]);

static size_t nextTxPowerIndex(int currentPower)
{
    for (size_t i = 0; i < kTxPowerLevelCount; ++i)
        if (kTxPowerLevels[i] == currentPower)
            return (i + 1) % kTxPowerLevelCount;
    return 0;
}

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

/// @brief Monitors an asynchronous connection and retries timed-out attempts at
/// each supported transmit-power level.
/// @param pvParameters Owned AsyncWiFiParameters instance.
void WiFi_Task(void *pvParameters)
{
    AsyncWiFiParameters parameters = *static_cast<AsyncWiFiParameters *>(pvParameters);
    delete static_cast<AsyncWiFiParameters *>(pvParameters);

    wl_status_t old_state = parameters.notifyInitialConnection
                                ? WL_DISCONNECTED
                                : WiFi.status();
    uint32_t attemptStartedAt = millis();
    size_t nextPower = nextTxPowerIndex(gTxPower);

    while (true)
    {
        const wl_status_t state = WiFi.status();
        if (state != old_state)
        {
            if (state == WL_CONNECTED)
            {
                wifiConnectedInternal();
                if (parameters.persistSuccessfulPower && gTxPower != parameters.configuredPower)
                {
                    const NightMare::WiFiProfile successful = {
                        parameters.ssid, parameters.password, gTxPower};
                    if (!savePowerProfile(successful))
                        LOG_ERROR("WiFi", "Could not persist successful tx power %d", gTxPower);
                    else
                        parameters.configuredPower = gTxPower;
                }
                if (parameters.deleteAfterConnect)
                {
                    WiFiTaskHandle = NULL;
                    wifiTaskStaysActive = false;
                    wifiTaskPersistsPower = false;
                    vTaskDelete(NULL);
                    return;
                }
            }
            else if (old_state == WL_CONNECTED)
            {
                attemptStartedAt = millis();
                nextPower = nextTxPowerIndex(gTxPower);
                WiFi.reconnect();
            }
            old_state = state;
        }

        if (state != WL_CONNECTED && millis() - attemptStartedAt >= AsyncConnectionAttemptMs)
        {
            const int retryPower = kTxPowerLevels[nextPower];
            nextPower = (nextPower + 1) % kTxPowerLevelCount;
            LOG_WARNING("WiFi", "Connection timed out; retrying with tx power %d quarter-dBm",
                        retryPower);

            WiFi.disconnect(false, false);
            gTxPower = retryPower;
            if (!applyTxPower(gTxPower))
            {
                LOG_ERROR("WiFi", "Could not apply retry tx power %d", gTxPower, "reverting to previous power %d", parameters.configuredPower);

            }
            else
                WiFi.begin(parameters.ssid.c_str(), parameters.password.c_str());
            attemptStartedAt = millis();
        }

        const int delayTime = state == WL_CONNECTED ? 5000 : 100;
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
    return startAsyncConnection(ssid, password, deleteAfterConnect, false);
}

namespace
{
    bool createAsyncTask(const char *ssid, const char *password,
                         bool deleteAfterConnect, bool persistSuccessfulPower,
                         bool notifyInitialConnection)
    {
        if (WiFiTaskHandle)
            return false;

        AsyncWiFiParameters *parameters = new AsyncWiFiParameters{
            String(ssid), String(password), deleteAfterConnect, persistSuccessfulPower,
            notifyInitialConnection, gTxPower};
        wifiTaskStaysActive = !deleteAfterConnect;
        wifiTaskPersistsPower = persistSuccessfulPower;
        // tskNO_AFFINITY instead of core 1: the ESP32-C6 (and C3/H2/S2) is
        // single-core, so pinning to core 1 fails configASSERT and panics.
        const BaseType_t result = xTaskCreatePinnedToCore(WiFi_Task,
                                                          "WiFi_Task",
                                                          4096,
                                                          parameters,
                                                          1,
                                                          &WiFiTaskHandle,
                                                          tskNO_AFFINITY);
        if (result != pdPASS)
        {
            delete parameters;
            WiFiTaskHandle = nullptr;
            wifiTaskStaysActive = false;
            wifiTaskPersistsPower = false;
        }
        LOG("WiFi", "%s TASK: Created WiFi task for SSID: %s", OK_LOG(result == pdPASS), ssid);

        return result == pdPASS;
    }

    bool startAsyncConnection(const char *ssid, const char *password,
                              bool deleteAfterConnect, bool persistSuccessfulPower)
    {
        if (WiFiTaskHandle)
            return false;

        WiFi.disconnect(true, true); // disconnect and erase old credentials
        WiFi.mode(WIFI_STA);
        if (!applyTxPower(gTxPower))
        {
            LOG_ERROR("WiFi", "Could not apply tx power %d", gTxPower);
            return false;
        }
        WiFi.setHostname(gDeviceIdentity.getDeviceName().c_str());
        WiFi.setAutoReconnect(true);
        gDeviceIdentity.lockAddress();
        // See WiFi_Connect: clears any stale status left by a prior scan/connect
        WiFi.begin(ssid, password);

        return createAsyncTask(ssid, password, deleteAfterConnect,
                               persistSuccessfulPower, true);
    }

    bool startAsyncMonitor(const char *ssid, const char *password,
                           bool persistSuccessfulPower)
    {
        WiFi.setAutoReconnect(true);
        return createAsyncTask(ssid, password, false, persistSuccessfulPower, false);
    }
}

/// @brief Disconnects from the WiFi network
void WiFi_Disconnect()
{
    if (WiFiTaskHandle)
        WiFi_cancelAsyncConnect();
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
    return startAsyncConnection(profile.ssid.c_str(), profile.password.c_str(), false, true);
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
    // Cast first: wifi_power_t is an enum, and arithmetic straight from it to
    // float is deprecated.
    return static_cast<int>(WiFi.getTxPower()) / 4.0f;
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

namespace
{
    bool savePowerProfile(const NightMare::WiFiProfile &profile)
    {
        const String keys[] = {NightMare::PersistentKey::WifiSsid, NightMare::PersistentKey::WifiPassword,
                               NightMare::PersistentKey::WifiTxPower};
        const String values[] = {profile.ssid, profile.password, String(profile.txPower)};
        return PersistentSettings.setMany(keys, values, 3);
    }
}

bool WiFi_changeProfile(const NightMare::WiFiProfile &profile, bool force)
{
    if (force)
    {
        LOG_WARNING("WiFi", "Forcing WiFi profile change. Configuration will be saved even if not valid. SSID: %s, txPower: %d ", profile.ssid.c_str(), profile.txPower);
    }
    if (!WiFi_isValidTxPower(profile.txPower))
    {
        LOG_ERROR("WiFi", "Invalid tx power %d", profile.txPower);
        if (!(force))
            return false;
    }
    NightMare::WiFiProfile old = WiFi_getProfile();
    const bool resumeMonitoring = WiFiTaskHandle && wifiTaskStaysActive;
    const bool persistFallbackPower = wifiTaskPersistsPower;
    if (WiFiTaskHandle)
        WiFi_cancelAsyncConnect();
    WiFi.disconnect();
    gTxPower = profile.txPower;
    if (!WiFi_Connect(profile.ssid.c_str(), profile.password.c_str(), 15000) && !(force))
    {
        gTxPower = old.txPower;
        startAsyncConnection(old.ssid.c_str(), old.password.c_str(),
                             !resumeMonitoring, persistFallbackPower);
        return false;
    }
    // One write for the whole profile: a partial one would pair a new network
    // with the old power, or the reverse.
    const bool saved = savePowerProfile(profile);
    if (resumeMonitoring)
    {
        const bool monitoring = WiFi.status() == WL_CONNECTED
                                    ? startAsyncMonitor(profile.ssid.c_str(), profile.password.c_str(),
                                                        persistFallbackPower)
                                    : startAsyncConnection(profile.ssid.c_str(), profile.password.c_str(),
                                                           false, persistFallbackPower);
        if (!monitoring)
            LOG_ERROR("WiFi", "Could not resume asynchronous connection recovery");
    }
    return saved;
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

bool WiFi_cancelAsyncConnect()
{
    if (WiFiTaskHandle)
    {
        vTaskDelete(WiFiTaskHandle);
        WiFiTaskHandle = NULL;
        wifiTaskStaysActive = false;
        wifiTaskPersistsPower = false;
        return true;
    }
    else
    {
        LOG_WARNING("WiFi", "No async WiFi connection task to cancel.");
    }
    return false;
}
#endif // NM_ENABLE_WIFI
