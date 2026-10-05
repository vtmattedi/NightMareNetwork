#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_WIFI

// ESP-IDF only: no Arduino, PersistentSettings or NightMare identity here.
// Persistence, hostname and follow-up services belong to the caller.
//
// The IP station: joining an AP and getting an address. It runs on the Wi-Fi
// radio but does not own it -- Network/WiFiRadio does. Starting the station
// starts the radio if needed; stopping it leaves the radio up, since ESP-NOW
// may be using it.
#include <esp_wifi.h>
#include <cstddef>
#include <cstdint>
#include <string>

namespace NightMare
{
constexpr int NM_TX_POWER_AUTO = 0;

struct WiFiProfile
{
    std::string ssid;
    std::string password;
    int txPower = NM_TX_POWER_AUTO;
};

enum class WiFiState : uint8_t
{
    STOPPED = 0,
    CONNECTING,
    CONNECTED,
    DISCONNECTED
};

// Snapshot of the stack. rssi and channel are 0 unless CONNECTED; txPowerDbm is
// 0 while STOPPED.
struct WiFiInfo
{
    WiFiState state = WiFiState::STOPPED;
    std::string ssid;
    std::string bssid;
    std::string ip;
    int txPower = NM_TX_POWER_AUTO; // in use; a fallback level replaces the requested one
    float txPowerDbm = 0;
    int8_t rssi = 0;
    uint8_t channel = 0;
};

struct WiFiScanResult
{
    std::string ssid;
    std::string bssid;
    int8_t rssi = 0;
    uint8_t channel = 0;
    wifi_auth_mode_t authMode = WIFI_AUTH_OPEN;
};
}

using WiFiStateCallback = void (*)(NightMare::WiFiState state);

// Runs on every state change, from the caller of WiFi_start/WiFi_stop or from
// the station's monitor task -- never the ESP event task, so it may do real
// work (the connected chain starts OTA, SNTP and the network connection).
void WiFi_onState(WiFiStateCallback callback);

// Starts the station with the given profile and keeps it connected, cycling
// TX power levels on repeated failure. Starts the radio if it is not running.
// Idempotent while connecting/connected.
bool WiFi_start(const NightMare::WiFiProfile &profile, const char *hostname = nullptr);
// Stops the station: disconnects, forgets the AP and ends the recovery task.
// The radio stays up (ESP-NOW may be on it); WiFiRadio_stop() turns it off.
void WiFi_stop();
// Tries the profile synchronously (up to 15 s). On failure the previous profile
// is restored and false returned. Nothing is persisted; false while stopped.
bool WiFi_changeProfile(const NightMare::WiFiProfile &profile);
// STOPPED, or running: CONNECTING, CONNECTED, DISCONNECTED.
NightMare::WiFiState WiFi_state();
NightMare::WiFiInfo WiFi_info();

bool WiFi_startScan();
bool WiFi_scanInProgress();
int WiFi_scanCount();
bool WiFi_scanComplete();
bool WiFi_scanResult(size_t index, NightMare::WiFiScanResult &result);
void WiFi_clearScanResults();
// Aborts an outstanding asynchronous scan. Safe when no scan is running.
void WiFi_abortScan();
void WiFi_scanTick();

const char *WiFi_getAuthTypeName(wifi_auth_mode_t authType);
const char *WiFi_stateName(NightMare::WiFiState status);
bool WiFi_isValidTxPower(int quarterDbm);

#endif // NM_ENABLE_WIFI
