#include <NightMare/Features.h>
#if NM_ENABLE_WIFI

#include "NmWifiEsp.h"

#include <Core/DeviceIdentity.h>
#include <Core/Logs.h>
#include <Core/PersistentKeys.h>
#include <Core/StateStore.h>
#include <Network/NmConnection.h>
#if NM_ENABLE_OTA
#include <Util/OTA.h>
#endif
#if NM_ENABLE_TIME_SYNC
#include <Util/TimeSyncronization.h>
#endif
#include <creds.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <cstring>

#if !defined(DEFAULT_SSID) || !defined(DEFAULT_PASSWORD)
#error "Please define DEFAULT_SSID and DEFAULT_PASSWORD in creds.h"
#endif

int gTxPower = NightMare::NM_TX_POWER_AUTO;

namespace
{
constexpr uint32_t AttemptTimeoutMs = 15000;
constexpr size_t MaxScanResults = 32;
const int8_t TxPowerLevels[] = {84, 82, 80, 78, 76, 74, 68, 60,
                                52, 44, 34, 28, 20, 8, -4};
constexpr size_t TxPowerLevelCount = sizeof(TxPowerLevels) / sizeof(TxPowerLevels[0]);

SemaphoreHandle_t stateMutex = nullptr;
TaskHandle_t monitorTaskHandle = nullptr;
esp_netif_t *stationNetif = nullptr;
esp_event_handler_instance_t wifiEvents = nullptr;
esp_event_handler_instance_t ipEvents = nullptr;
NightMare::WiFiStatus currentStatus = NightMare::WiFiStatus::STOPPED;
NightMare::WiFiProfile activeProfile;
NightMare::WiFiScanResult scanResults[MaxScanResults];
size_t scanResultCount = 0;
bool scanRunning = false;
bool driverInitialized = false;
bool keepMonitoring = false;
bool persistFallbackPower = false;
bool firstConnection = true;
bool servicesStarted = false;
String currentIp;
WiFiConnectedCallback connectedCallback = nullptr;

bool ensureMutex()
{
    if (stateMutex == nullptr)
        stateMutex = xSemaphoreCreateMutex();
    return stateMutex != nullptr;
}

void setStatus(NightMare::WiFiStatus status)
{
    if (!ensureMutex())
        return;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    currentStatus = status;
    xSemaphoreGive(stateMutex);
}

size_t nextPowerIndex(int power)
{
    for (size_t i = 0; i < TxPowerLevelCount; ++i)
        if (TxPowerLevels[i] == power)
            return (i + 1) % TxPowerLevelCount;
    return 0;
}

bool applyTxPower(int quarterDbm)
{
    if (quarterDbm == NightMare::NM_TX_POWER_AUTO)
        return true;
    return WiFi_isValidTxPower(quarterDbm) &&
           esp_wifi_set_max_tx_power(static_cast<int8_t>(quarterDbm)) == ESP_OK;
}

bool saveProfile(const NightMare::WiFiProfile &profile)
{
    const String keys[] = {NightMare::PersistentKey::WifiSsid,
                           NightMare::PersistentKey::WifiPassword,
                           NightMare::PersistentKey::WifiTxPower};
    const String values[] = {profile.ssid, profile.password, String(profile.txPower)};
    return PersistentSettings.setMany(keys, values, 3);
}

bool configureStation(const NightMare::WiFiProfile &profile)
{
    if (profile.ssid.length() == 0 || profile.ssid.length() > 32 ||
        profile.password.length() > 64)
        return false;
    wifi_config_t config = {};
    memcpy(config.sta.ssid, profile.ssid.c_str(), profile.ssid.length());
    memcpy(config.sta.password, profile.password.c_str(), profile.password.length());
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    return esp_wifi_set_config(WIFI_IF_STA, &config) == ESP_OK;
}

void startFrameworkServices()
{
    if (servicesStarted)
        return;
    servicesStarted = true;
#if NM_ENABLE_OTA
    initOTA();
#endif
#if NM_ENABLE_NETWORK && (NM_NETWORK_MQTT || NM_NETWORK_LOCALMQTT || NM_NETWORK_ESPNOW)
    NightMare::ConnectionType connection = static_cast<NightMare::ConnectionType>(
        NightMare::preferredConnection.value());
    bool started = NightMare::SelectConnection(connection);
#if NM_NETWORK_MQTT
    if (!started && connection != NightMare::ConnectionType::MQTT)
        started = NightMare::SelectConnection(NightMare::ConnectionType::MQTT);
#elif NM_NETWORK_LOCALMQTT
    if (!started && connection != NightMare::ConnectionType::LOCAL_MQTT)
        started = NightMare::SelectConnection(NightMare::ConnectionType::LOCAL_MQTT);
#endif
    if (!started)
        LOG_ERROR("NET", "Could not start the preferred connection");
#endif
#if NM_ENABLE_TIME_SYNC
    if (!startSntpTimeSync())
        LOG_ERROR("Time", "Could not start SNTP synchronization");
#endif
}

void notifyConnected()
{
    const bool wasFirst = firstConnection;
    startFrameworkServices();
    LOG("WiFi", "Connected to %s, IP: %s",
        WiFi_currentSSID().c_str(), WiFi_localIP().c_str());
    if (connectedCallback != nullptr)
        connectedCallback(wasFirst);
    firstConnection = false;
}

void handleIpEvent(void *, esp_event_base_t, int32_t eventId, void *eventData)
{
    if (eventId != IP_EVENT_STA_GOT_IP)
        return;
    const auto *event = static_cast<ip_event_got_ip_t *>(eventData);
    char address[16] = {};
    snprintf(address, sizeof(address), IPSTR, IP2STR(&event->ip_info.ip));
    if (ensureMutex())
    {
        xSemaphoreTake(stateMutex, portMAX_DELAY);
        currentIp = address;
        currentStatus = NightMare::WiFiStatus::CONNECTED;
        xSemaphoreGive(stateMutex);
    }
    notifyConnected();
}

void handleWiFiEvent(void *, esp_event_base_t, int32_t eventId, void *)
{
    if (eventId == WIFI_EVENT_STA_START)
    {
        setStatus(NightMare::WiFiStatus::CONNECTING);
        esp_wifi_connect();
    }
    else if (eventId == WIFI_EVENT_STA_DISCONNECTED)
    {
        if (ensureMutex())
        {
            xSemaphoreTake(stateMutex, portMAX_DELAY);
            currentIp = String();
            if (currentStatus != NightMare::WiFiStatus::STOPPED)
                currentStatus = NightMare::WiFiStatus::DISCONNECTED;
            xSemaphoreGive(stateMutex);
        }
        if (keepMonitoring)
            esp_wifi_connect();
    }
    else if (eventId == WIFI_EVENT_SCAN_DONE)
    {
        uint16_t count = MaxScanResults;
        wifi_ap_record_t records[MaxScanResults] = {};
        if (esp_wifi_scan_get_ap_records(&count, records) == ESP_OK && ensureMutex())
        {
            xSemaphoreTake(stateMutex, portMAX_DELAY);
            scanResultCount = count;
            for (size_t i = 0; i < count; ++i)
            {
                scanResults[i].ssid = reinterpret_cast<const char *>(records[i].ssid);
                char bssid[18] = {};
                snprintf(bssid, sizeof(bssid), "%02x:%02x:%02x:%02x:%02x:%02x",
                         records[i].bssid[0], records[i].bssid[1], records[i].bssid[2],
                         records[i].bssid[3], records[i].bssid[4], records[i].bssid[5]);
                scanResults[i].bssid = bssid;
                scanResults[i].rssi = records[i].rssi;
                scanResults[i].channel = records[i].primary;
                scanResults[i].authMode = records[i].authmode;
            }
            scanRunning = false;
            xSemaphoreGive(stateMutex);
        }
        else
        {
            scanRunning = false;
        }
    }
}

bool initializeDriver()
{
    if (driverInitialized)
        return true;
    esp_err_t result = esp_netif_init();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE)
        return false;
    result = esp_event_loop_create_default();
    if (result != ESP_OK && result != ESP_ERR_INVALID_STATE)
        return false;
    stationNetif = esp_netif_create_default_wifi_sta();
    if (stationNetif == nullptr)
        return false;
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&config) != ESP_OK ||
        esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
        esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK)
        return false;
    if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            handleWiFiEvent, nullptr,
                                            &wifiEvents) != ESP_OK ||
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            handleIpEvent, nullptr,
                                            &ipEvents) != ESP_OK)
        return false;
    driverInitialized = true;
    return true;
}

bool beginConnection(const NightMare::WiFiProfile &profile, bool monitor)
{
    if (!initializeDriver() || !WiFi_isValidTxPower(profile.txPower))
        return false;
    activeProfile = profile;
    gTxPower = profile.txPower;
    keepMonitoring = monitor;
    gDeviceIdentity.lockAddress();
    esp_netif_set_hostname(stationNetif, gDeviceIdentity.getDeviceName().c_str());
    esp_wifi_disconnect();
    if (!configureStation(profile) || !applyTxPower(profile.txPower))
        return false;
    setStatus(NightMare::WiFiStatus::CONNECTING);
    const esp_err_t started = esp_wifi_start();
    if (started != ESP_OK && started != ESP_ERR_WIFI_CONN)
        return false;
    const esp_err_t connected = esp_wifi_connect();
    return connected == ESP_OK || connected == ESP_ERR_WIFI_CONN;
}

void monitorTask(void *)
{
    uint32_t attemptStarted = millis();
    size_t powerIndex = nextPowerIndex(gTxPower);
    while (keepMonitoring)
    {
        if (WiFi_isConnected())
        {
            attemptStarted = millis();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (millis() - attemptStarted >= AttemptTimeoutMs)
        {
            const int retryPower = TxPowerLevels[powerIndex];
            powerIndex = (powerIndex + 1) % TxPowerLevelCount;
            esp_wifi_disconnect();
            if (applyTxPower(retryPower))
            {
                gTxPower = retryPower;
                esp_wifi_connect();
                if (persistFallbackPower)
                {
                    activeProfile.txPower = retryPower;
                    saveProfile(activeProfile);
                }
            }
            attemptStarted = millis();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    monitorTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

bool startMonitor(bool persistPower)
{
    persistFallbackPower = persistPower;
    keepMonitoring = true;
    if (monitorTaskHandle != nullptr)
        return true;
    return xTaskCreate(monitorTask, "wifi_monitor", 4096, nullptr, 1,
                       &monitorTaskHandle) == pdPASS;
}
}

void WiFi_onConnected(WiFiConnectedCallback callback) { connectedCallback = callback; }

bool WiFi_isValidTxPower(int quarterDbm)
{
    if (quarterDbm == NightMare::NM_TX_POWER_AUTO)
        return true;
    for (const int8_t level : TxPowerLevels)
        if (quarterDbm == level)
            return true;
    return false;
}

bool WiFi_Connect(const char *ssid, const char *password, int timeoutMs,
                  void *waitCallback(unsigned int))
{
    NightMare::WiFiProfile profile{String(ssid == nullptr ? "" : ssid),
                                   String(password == nullptr ? "" : password),
                                   gTxPower};
    if (!beginConnection(profile, false))
        return false;
    const uint32_t started = millis();
    while (!WiFi_isConnected())
    {
        if (waitCallback != nullptr)
            waitCallback(millis() - started);
        if (timeoutMs > 0 && millis() - started >= static_cast<uint32_t>(timeoutMs))
            return false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return true;
}

bool WiFi_ConnectAsync(const char *ssid, const char *password, bool deleteAfterConnect)
{
    NightMare::WiFiProfile profile{String(ssid == nullptr ? "" : ssid),
                                   String(password == nullptr ? "" : password),
                                   gTxPower};
    if (!beginConnection(profile, !deleteAfterConnect))
        return false;
    return deleteAfterConnect || startMonitor(false);
}

void WiFi_Disconnect()
{
    WiFi_cancelAsyncConnect();
    if (driverInitialized)
    {
        esp_wifi_disconnect();
        esp_wifi_stop();
    }
    setStatus(NightMare::WiFiStatus::STOPPED);
}

NightMare::WiFiProfile WiFi_getProfile()
{
    PersistentSettings.begin();
    NightMare::WiFiProfile profile;
    profile.ssid = PersistentSettings.getOrSave(NightMare::PersistentKey::WifiSsid,
                                                 DEFAULT_SSID);
    profile.password = PersistentSettings.getOrSave(NightMare::PersistentKey::WifiPassword,
                                                     DEFAULT_PASSWORD);
    profile.txPower = PersistentSettings.getOrSave(NightMare::PersistentKey::WifiTxPower,
                                                    String(NightMare::NM_TX_POWER_AUTO)).toInt();
    return profile;
}

bool WiFi_Auto()
{
    const NightMare::WiFiStatus status = WiFi_status();
    if (status == NightMare::WiFiStatus::CONNECTED ||
        status == NightMare::WiFiStatus::CONNECTING)
        return true;
    const NightMare::WiFiProfile profile = WiFi_getProfile();
    if (!beginConnection(profile, true))
        return false;
    return startMonitor(true);
}

bool WiFi_changeProfile(const NightMare::WiFiProfile &profile, bool force)
{
    if (!WiFi_isValidTxPower(profile.txPower) && !force)
        return false;
    const NightMare::WiFiProfile previous = WiFi_getProfile();
    WiFi_cancelAsyncConnect();
    gTxPower = profile.txPower;
    if (!WiFi_Connect(profile.ssid.c_str(), profile.password.c_str(), AttemptTimeoutMs) &&
        !force)
    {
        gTxPower = previous.txPower;
        beginConnection(previous, true);
        startMonitor(true);
        return false;
    }
    const bool saved = saveProfile(profile);
    beginConnection(profile, true);
    startMonitor(true);
    return saved;
}

bool WiFi_ChangeCredentials(const String &ssid, const String &password)
{
    NightMare::WiFiProfile profile = WiFi_getProfile();
    profile.ssid = ssid;
    profile.password = password;
    return WiFi_changeProfile(profile);
}

bool WiFi_setTxPower(int quarterDbm)
{
    NightMare::WiFiProfile profile = WiFi_getProfile();
    profile.txPower = quarterDbm;
    return WiFi_changeProfile(profile);
}

float WiFi_getTxPowerDbm()
{
    if (!driverInitialized)
        return NightMare::NM_TX_POWER_AUTO;
    int8_t power = 0;
    return esp_wifi_get_max_tx_power(&power) == ESP_OK
               ? static_cast<float>(power) / 4.0f
               : NightMare::NM_TX_POWER_AUTO;
}

bool WiFi_cancelAsyncConnect()
{
    if (monitorTaskHandle == nullptr)
        return true;
    keepMonitoring = false;
    for (int i = 0; i < 50 && monitorTaskHandle != nullptr; ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    return monitorTaskHandle == nullptr;
}

bool WiFi_isConnected() { return WiFi_status() == NightMare::WiFiStatus::CONNECTED; }

NightMare::WiFiStatus WiFi_status()
{
    if (!ensureMutex())
        return NightMare::WiFiStatus::FAILED;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    const NightMare::WiFiStatus status = currentStatus;
    xSemaphoreGive(stateMutex);
    return status;
}

String WiFi_localIP()
{
    if (!ensureMutex())
        return String();
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    const String value = currentIp;
    xSemaphoreGive(stateMutex);
    return value;
}

String WiFi_currentSSID()
{
    if (!ensureMutex())
        return String();
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    const String value = activeProfile.ssid;
    xSemaphoreGive(stateMutex);
    return value;
}

int WiFi_RSSI()
{
    wifi_ap_record_t record = {};
    return WiFi_isConnected() && esp_wifi_sta_get_ap_info(&record) == ESP_OK
               ? record.rssi
               : 0;
}

bool WiFi_startScan()
{
    if (!initializeDriver() || scanRunning)
        return false;
    scanRunning = true;
    scanResultCount = 0;
    wifi_scan_config_t config = {};
    config.show_hidden = true;
    if (esp_wifi_scan_start(&config, false) == ESP_OK)
        return true;
    scanRunning = false;
    return false;
}

bool WiFi_scanInProgress() { return scanRunning; }
int WiFi_scanCount() { return scanRunning ? -1 : static_cast<int>(scanResultCount); }

bool WiFi_scanResult(size_t index, NightMare::WiFiScanResult &result)
{
    if (!ensureMutex())
        return false;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    const bool exists = index < scanResultCount;
    if (exists)
        result = scanResults[index];
    xSemaphoreGive(stateMutex);
    return exists;
}

void WiFi_clearScan()
{
    if (!ensureMutex())
        return;
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    scanResultCount = 0;
    xSemaphoreGive(stateMutex);
}

void WiFi_Scan()
{
    if (!WiFi_startScan())
        return;
    while (WiFi_scanInProgress())
        vTaskDelay(pdMS_TO_TICKS(20));
    for (int i = 0; i < WiFi_scanCount(); ++i)
    {
        NightMare::WiFiScanResult result;
        if (WiFi_scanResult(i, result))
            LOG("WiFi", "%d: %s (%d) %s", i + 1, result.ssid.c_str(),
                result.rssi, WiFi_getAuthTypeName(result.authMode));
    }
    WiFi_clearScan();
}

const char *WiFi_getStatusName(NightMare::WiFiStatus status)
{
    switch (status)
    {
    case NightMare::WiFiStatus::STOPPED: return "Stopped";
    case NightMare::WiFiStatus::CONNECTING: return "Connecting";
    case NightMare::WiFiStatus::CONNECTED: return "Connected";
    case NightMare::WiFiStatus::DISCONNECTED: return "Disconnected";
    case NightMare::WiFiStatus::FAILED: return "Failed";
    }
    return "Unknown";
}

const char *WiFi_getAuthTypeName(wifi_auth_mode_t authType)
{
    switch (authType)
    {
    case WIFI_AUTH_OPEN: return "Open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA-PSK";
    case WIFI_AUTH_WPA2_PSK: return "WPA2-PSK";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2-PSK";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-Enterprise";
    case WIFI_AUTH_WPA3_PSK: return "WPA3-PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3-PSK";
    default: return "Unknown";
    }
}

#endif // NM_ENABLE_WIFI
