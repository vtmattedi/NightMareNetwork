#include <NightMare/Features.h>
#if NM_ENABLE_WIFI

#include "NmWifiEsp.h"

#include <Core/Logs.h>
#include <Network/WiFiRadio/NmWifiRadio.h>
#if NM_NETWORK_ESPNOW
#include <Network/EspNow/NmEspNowConnection.h>
#endif
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <cstdio>
#include <cstring>

namespace
{
int gTxPower = NightMare::NM_TX_POWER_AUTO;
constexpr char Tag[] = "WiFi";
constexpr uint32_t AttemptTimeoutMs = 15000;
constexpr size_t MaxScanResults = 32;
const int8_t TxPowerLevels[] = {84, 82, 80, 78, 76, 74, 68, 60,
                                52, 44, 34, 28, 20, 8, -4};
constexpr size_t TxPowerLevelCount = sizeof(TxPowerLevels) / sizeof(TxPowerLevels[0]);

SemaphoreHandle_t stateMutex = nullptr;
TaskHandle_t monitorTaskHandle = nullptr;
esp_event_handler_instance_t wifiEvents = nullptr;
esp_event_handler_instance_t ipEvents = nullptr;
NightMare::WiFiState currentState = NightMare::WiFiState::STOPPED;
NightMare::WiFiProfile activeProfile;
std::string hostname;
NightMare::WiFiScanResult scanResults[MaxScanResults];
size_t scanResultCount = 0;
bool scanRunning = false;
bool scanCompleted = false;
bool scanResumePending = false;
bool scanSuspendedEspNow = false;
uint32_t scanStartedMs = 0;
constexpr uint32_t ScanTimeoutMs = 30000;
bool handlersRegistered = false;
bool keepMonitoring = false;
std::string currentIp;
WiFiStateCallback stateCallback = nullptr;

portMUX_TYPE mutexInit = portMUX_INITIALIZER_UNLOCKED;

bool ensureMutex()
{
    portENTER_CRITICAL(&mutexInit);
    const bool needed = stateMutex == nullptr;
    portEXIT_CRITICAL(&mutexInit);
    if (needed)
    {
        SemaphoreHandle_t created = xSemaphoreCreateMutex();
        portENTER_CRITICAL(&mutexInit);
        if (stateMutex == nullptr)
        {
            stateMutex = created;
            created = nullptr;
        }
        portEXIT_CRITICAL(&mutexInit);
        if (created != nullptr)
            vSemaphoreDelete(created);
    }
    return stateMutex != nullptr;
}

// stateMutex guards every field below except the ESP handles and the two
// task flags: currentState, currentIp, activeProfile, gTxPower, hostname and
// all scan state. It is not recursive: never call a locking helper while held,
// and never invoke a callback while held.
class Lock
{
public:
    Lock() { held_ = ensureMutex() && xSemaphoreTake(stateMutex, portMAX_DELAY) == pdTRUE; }
    ~Lock()
    {
        if (held_)
            xSemaphoreGive(stateMutex);
    }
    Lock(const Lock &) = delete;
    Lock &operator=(const Lock &) = delete;

private:
    bool held_ = false;
};

bool isConnected() { return WiFi_state() == NightMare::WiFiState::CONNECTED; }

uint32_t nowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// The Wi-Fi/IP event handlers run on the default event loop task (sys_evt), whose
// stack has room for neither a formatted log line nor the connected-callback
// chain (OTA, SNTP, NVS, starting the connection) -- doing either there overflows
// it. The handlers only record what happened; monitorTask logs it and delivers the
// state callback, on a stack sized for that.
portMUX_TYPE pendingLock = portMUX_INITIALIZER_UNLOCKED;
bool pendingStarted = false;
uint8_t pendingChannel = 0; // associated on this channel; 0 = nothing to report
uint8_t pendingDisconnects = 0;
uint8_t pendingReason = 0;
volatile NightMare::WiFiState lastPublished = NightMare::WiFiState::STOPPED;

void publishState(NightMare::WiFiState status)
{
    lastPublished = status;
    if (stateCallback != nullptr)
        stateCallback(status);
}

// For callers on an ordinary task: records and publishes right away.
void setStatus(NightMare::WiFiState status)
{
    bool changed;
    {
        Lock lock;
        changed = currentState != status;
        currentState = status;
    }
    if (changed)
        publishState(status);
}

// For the event handlers: records only. monitorTask publishes it.
void recordStatus(NightMare::WiFiState status)
{
    Lock lock;
    currentState = status;
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

bool configureStation(const NightMare::WiFiProfile &profile)
{
    if (profile.ssid.size() == 0 || profile.ssid.size() > 32 ||
        profile.password.size() > 64)
        return false;
    wifi_config_t config = {};
    memcpy(config.sta.ssid, profile.ssid.c_str(), profile.ssid.size());
    memcpy(config.sta.password, profile.password.c_str(), profile.password.size());
    config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    return esp_wifi_set_config(WIFI_IF_STA, &config) == ESP_OK;
}

void notifyConnected()
{
    std::string ssid;
    std::string ip;
    {
        Lock lock;
        ssid = activeProfile.ssid;
        ip = currentIp;
    }
    LOG(Tag, "Connected to %s, IP: %s", ssid.c_str(), ip.c_str());
    publishState(NightMare::WiFiState::CONNECTED);
}

void handleIpEvent(void *, esp_event_base_t, int32_t eventId, void *eventData)
{
    if (eventId != IP_EVENT_STA_GOT_IP)
        return;
    const auto *event = static_cast<ip_event_got_ip_t *>(eventData);
    char address[16] = {};
    snprintf(address, sizeof(address), IPSTR, IP2STR(&event->ip_info.ip));
    {
        Lock lock;
        currentIp = address;
        currentState = NightMare::WiFiState::CONNECTED;
    }
    // notifyConnected() runs from monitorTask; see pendingLock.
}

void handleWiFiEvent(void *, esp_event_base_t, int32_t eventId, void *eventData)
{
    if (eventId == WIFI_EVENT_STA_START)
    {
        // STA_START is the radio starting, which no longer implies a station:
        // a radio-only (ESP-NOW) device must not start joining an AP here.
        if (!keepMonitoring)
            return;
        portENTER_CRITICAL(&pendingLock);
        pendingStarted = true;
        portEXIT_CRITICAL(&pendingLock);
        recordStatus(NightMare::WiFiState::CONNECTING);
        esp_wifi_connect();
    }
    else if (eventId == WIFI_EVENT_STA_CONNECTED)
    {
        const auto *event = static_cast<const wifi_event_sta_connected_t *>(eventData);
        portENTER_CRITICAL(&pendingLock);
        pendingChannel = event != nullptr ? event->channel : 0;
        portEXIT_CRITICAL(&pendingLock);
    }
    else if (eventId == WIFI_EVENT_STA_DISCONNECTED)
    {
        const auto *event = static_cast<const wifi_event_sta_disconnected_t *>(eventData);
        portENTER_CRITICAL(&pendingLock);
        if (pendingDisconnects < UINT8_MAX)
            ++pendingDisconnects;
        pendingReason = event != nullptr ? event->reason : 0;
        portEXIT_CRITICAL(&pendingLock);
        {
            Lock lock;
            currentIp.clear();
        }
        if (WiFi_state() != NightMare::WiFiState::STOPPED)
            recordStatus(NightMare::WiFiState::DISCONNECTED);
        if (keepMonitoring)
            esp_wifi_connect();
    }
    else if (eventId == WIFI_EVENT_SCAN_DONE)
    {
        uint16_t count = MaxScanResults;
        wifi_ap_record_t records[MaxScanResults] = {};
        const bool read = esp_wifi_scan_get_ap_records(&count, records) == ESP_OK;
        Lock lock;
        if (read)
        {
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
        }
        scanRunning = false;
        scanCompleted = true;
        scanResumePending = scanSuspendedEspNow;
    }
}

// Hooks this layer's handlers onto the default event loop, once. They stay for
// the life of the process: the loop outlives any radio stop/start.
bool registerHandlers()
{
    if (handlersRegistered)
        return true;
    if (esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                            handleWiFiEvent, nullptr,
                                            &wifiEvents) != ESP_OK ||
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                            handleIpEvent, nullptr,
                                            &ipEvents) != ESP_OK)
    {
        LOG_ERROR(Tag, "Could not register the station event handlers");
        return false;
    }
    handlersRegistered = true;
    return true;
}

// The station runs on the radio but doesn't own it (Network/WiFiRadio does).
bool ensureRadioReady()
{
    if (!WiFiRadio_start())
    {
        LOG_ERROR(Tag, "The Wi-Fi radio is not running");
        return false;
    }
    return registerHandlers();
}

bool beginConnection(const NightMare::WiFiProfile &profile, bool monitor)
{
    if (!ensureRadioReady())
        return false;
    if (!WiFi_isValidTxPower(profile.txPower))
    {
        LOG_ERROR(Tag, "Invalid stored TX power %d", profile.txPower);
        return false;
    }
    {
        Lock lock;
        activeProfile = profile;
        gTxPower = profile.txPower;
    }
    keepMonitoring = monitor;
    std::string name;
    {
        Lock lock;
        name = hostname;
    }
    if (!name.empty() && WiFiRadio_stationNetif() != nullptr)
        esp_netif_set_hostname(WiFiRadio_stationNetif(), name.c_str());
    esp_wifi_disconnect();
    if (!configureStation(profile))
    {
        LOG_ERROR(Tag, "Could not configure station for '%s'", profile.ssid.c_str());
        return false;
    }
    setStatus(NightMare::WiFiState::CONNECTING);
    // The radio is already running (ensureRadioReady), which is also what
    // esp_wifi_set_max_tx_power needs: before esp_wifi_start it returns
    // ESP_ERR_WIFI_NOT_STARTED, and a non-AUTO power saved to NVS used to fail
    // every boot right here. A power that still won't apply is not worth
    // refusing to connect over: the driver default is used instead.
    if (!applyTxPower(profile.txPower))
        LOG_WARNING(Tag, "Could not apply TX power %d, using the driver default", profile.txPower);
    LOG(Tag, "Connecting to '%s'", profile.ssid.c_str());
    const esp_err_t connected = esp_wifi_connect();
    if (connected != ESP_OK && connected != ESP_ERR_WIFI_CONN)
    {
        LOG_ERROR(Tag, "esp_wifi_connect failed: %s", esp_err_to_name(connected));
        return false;
    }
    return true;
}

// Logs what the event handlers recorded and publishes a state they changed.
void reportPendingEvents()
{
    bool started;
    uint8_t channel;
    uint8_t disconnects;
    uint8_t reason;
    portENTER_CRITICAL(&pendingLock);
    started = pendingStarted;
    channel = pendingChannel;
    disconnects = pendingDisconnects;
    reason = pendingReason;
    pendingStarted = false;
    pendingChannel = 0;
    pendingDisconnects = 0;
    portEXIT_CRITICAL(&pendingLock);

    if (started)
        LOG(Tag, "Station started");
    // The reason code is the quickest way to tell a wrong password (15, 202,
    // 204), an AP that isn't there (201) and a plain drop apart.
    if (disconnects == 1)
        LOG_WARNING(Tag, "Disconnected, reason %u", reason);
    else if (disconnects > 1)
        LOG_WARNING(Tag, "Disconnected %u times, last reason %u", disconnects, reason);
    if (channel != 0)
        LOG(Tag, "Associated on channel %u, waiting for IP", channel);

    const NightMare::WiFiState state = WiFi_state();
    if (state == lastPublished)
        return;
    if (state == NightMare::WiFiState::CONNECTED)
        notifyConnected();
    else
        publishState(state);
}

void monitorTask(void *)
{
    uint32_t attemptStarted = nowMs();
    size_t powerIndex;
    {
        Lock lock;
        powerIndex = nextPowerIndex(gTxPower);
    }
    while (keepMonitoring)
    {
        reportPendingEvents();
        if (isConnected())
        {
            attemptStarted = nowMs();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (nowMs() - attemptStarted >= AttemptTimeoutMs)
        {
            const int retryPower = TxPowerLevels[powerIndex];
            powerIndex = (powerIndex + 1) % TxPowerLevelCount;
            esp_wifi_disconnect();
            if (applyTxPower(retryPower))
            {
                {
                    Lock lock;
                    gTxPower = retryPower;
                    activeProfile.txPower = retryPower;
                }
                esp_wifi_connect();
            }
            attemptStarted = nowMs();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    monitorTaskHandle = nullptr;
    vTaskDelete(nullptr);
}

bool startMonitor()
{
    keepMonitoring = true;
    if (monitorTaskHandle != nullptr)
        return true;
    // 6 KB: the state callback runs here, and on CONNECTED that starts OTA,
    // SNTP and the network connection, and saves to NVS.
    return xTaskCreate(monitorTask, "wifi_monitor", 6144, nullptr, 1,
                       &monitorTaskHandle) == pdPASS;
}

bool stopMonitor()
{
    if (monitorTaskHandle == nullptr)
        return true;
    keepMonitoring = false;
    for (int i = 0; i < 50 && monitorTaskHandle != nullptr; ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    return monitorTaskHandle == nullptr;
}

// Blocks until the station is associated with an IP or timeoutMs elapses.
bool connectBlocking(const NightMare::WiFiProfile &profile, uint32_t timeoutMs)
{
    if (!beginConnection(profile, false))
        return false;
    const uint32_t started = nowMs();
    while (!isConnected())
    {
        if (nowMs() - started >= timeoutMs)
            return false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return true;
}
}

void WiFi_onState(WiFiStateCallback callback) { stateCallback = callback; }

bool WiFi_isValidTxPower(int quarterDbm)
{
    if (quarterDbm == NightMare::NM_TX_POWER_AUTO)
        return true;
    for (const int8_t level : TxPowerLevels)
        if (quarterDbm == level)
            return true;
    return false;
}

void WiFi_stop()
{
    keepMonitoring = false;
    setStatus(NightMare::WiFiState::STOPPED);
    stopMonitor();
    if (WiFiRadio_running())
    {
        esp_wifi_disconnect();
        // Forget the AP. An empty station config is also what tells ESP-NOW
        // (EspNowClient::freeToTune) that no station owns the channel any more,
        // so it may hop again. The radio itself stays up for it.
        wifi_config_t none = {};
        esp_wifi_set_config(WIFI_IF_STA, &none);
    }
    {
        Lock lock;
        currentIp.clear();
        scanResultCount = 0;
        scanRunning = false;
        scanCompleted = false;
        scanResumePending = scanSuspendedEspNow;
    }
    WiFi_scanTick();
}

bool WiFi_start(const NightMare::WiFiProfile &profile, const char *stationHostname)
{
    const NightMare::WiFiState status = WiFi_state();
    if (status == NightMare::WiFiState::CONNECTED ||
        status == NightMare::WiFiState::CONNECTING)
        return true;
    {
        Lock lock;
        hostname = stationHostname != nullptr ? stationHostname : "";
    }
    if (!beginConnection(profile, true))
        return false;
    return startMonitor();
}

bool WiFi_changeProfile(const NightMare::WiFiProfile &profile)
{
    if (WiFi_state() == NightMare::WiFiState::STOPPED ||
        !WiFi_isValidTxPower(profile.txPower))
        return false;
    NightMare::WiFiProfile previous;
    {
        Lock lock;
        previous = activeProfile;
    }
    stopMonitor();
    if (!connectBlocking(profile, AttemptTimeoutMs))
    {
        {
            Lock lock;
            gTxPower = previous.txPower;
        }
        beginConnection(previous, true);
        startMonitor();
        return false;
    }
    return startMonitor();
}

NightMare::WiFiState WiFi_state()
{
    Lock lock;
    return currentState;
}

NightMare::WiFiInfo WiFi_info()
{
    NightMare::WiFiInfo info;
    {
        Lock lock;
        info.state = currentState;
        info.ssid = activeProfile.ssid;
        info.txPower = activeProfile.txPower;
        info.ip = currentIp;
    }
    if (info.state == NightMare::WiFiState::STOPPED || !WiFiRadio_running())
        return info;
    int8_t power = 0;
    if (esp_wifi_get_max_tx_power(&power) == ESP_OK)
        info.txPowerDbm = static_cast<float>(power) / 4.0f;
    wifi_ap_record_t record = {};
    if (info.state == NightMare::WiFiState::CONNECTED &&
        esp_wifi_sta_get_ap_info(&record) == ESP_OK)
    {
        info.rssi = record.rssi;
        info.channel = record.primary;
    }
    return info;
}

bool WiFi_startScan()
{
    // A scan needs the radio, not a station: it works on a radio-only device
    // too. It does not start the radio -- that is WiFiRadioBegin()'s call. On
    // a hopping ESP-NOW device the scan and the hop briefly fight over the
    // channel; the next hop sorts it out.
    if (!WiFiRadio_running() || !registerHandlers())
        return false;
    {
        Lock lock;
        if (scanRunning)
            return false;
    }
#if NM_NETWORK_ESPNOW
    const bool suspended = NmEspNowConnection::enabled() &&
                           NmEspNowConnection::suspend(
                               NightMare::ConnectivitySuspendReason::WIFI_SCAN);
#else
    const bool suspended = false;
#endif
    {
        Lock lock;
        scanRunning = true;
        scanResultCount = 0;
        scanCompleted = false;
        scanSuspendedEspNow = suspended;
        scanResumePending = false;
        scanStartedMs = nowMs();
    }
    wifi_scan_config_t config = {};
    config.show_hidden = true;
    if (esp_wifi_scan_start(&config, false) == ESP_OK)
        return true;
    {
        Lock lock;
        scanRunning = false;
        scanResumePending = scanSuspendedEspNow;
    }
    WiFi_scanTick();
    return false;
}

void WiFi_abortScan()
{
    bool wasRunning;
    {
        Lock lock;
        wasRunning = scanRunning;
        scanRunning = false;
        scanResumePending = scanSuspendedEspNow;
    }
    if (wasRunning)
        esp_wifi_scan_stop();
    WiFi_scanTick();
}

void WiFi_scanTick()
{
    bool timeout = false;
    bool resume = false;
    {
        Lock lock;
        timeout = scanRunning && nowMs() - scanStartedMs >= ScanTimeoutMs;
        if (timeout)
        {
            scanRunning = false;
            scanResumePending = scanSuspendedEspNow;
        }
        resume = scanResumePending;
        if (resume)
        {
            scanResumePending = false;
            scanSuspendedEspNow = false;
        }
    }
    if (timeout)
        esp_wifi_scan_stop();
#if NM_NETWORK_ESPNOW
    if (resume)
        NmEspNowConnection::resume(NightMare::ConnectivitySuspendReason::WIFI_SCAN);
#else
    (void)resume;
#endif
}

bool WiFi_scanInProgress()
{
    Lock lock;
    return scanRunning;
}

int WiFi_scanCount()
{
    Lock lock;
    return scanRunning ? -1 : static_cast<int>(scanResultCount);
}

bool WiFi_scanComplete()
{
    Lock lock;
    return scanCompleted && !scanRunning;
}

bool WiFi_scanResult(size_t index, NightMare::WiFiScanResult &result)
{
    Lock lock;
    const bool exists = index < scanResultCount;
    if (exists)
        result = scanResults[index];
    return exists;
}

void WiFi_clearScanResults()
{
    Lock lock;
    if (!scanRunning)
    {
        scanResultCount = 0;
        scanCompleted = false;
    }
}

const char *WiFi_stateName(NightMare::WiFiState status)
{
    switch (status)
    {
    case NightMare::WiFiState::STOPPED: return "Stopped";
    case NightMare::WiFiState::CONNECTING: return "Connecting";
    case NightMare::WiFiState::CONNECTED: return "Connected";
    case NightMare::WiFiState::DISCONNECTED: return "Disconnected";
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
