#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_WIFI

#include <Arduino.h>
#include <esp_wifi.h>

namespace NightMare
{
constexpr int NM_TX_POWER_AUTO = 0;

struct WiFiProfile
{
    String ssid;
    String password;
    int txPower = NM_TX_POWER_AUTO;
};

enum class WiFiStatus : uint8_t
{
    STOPPED = 0,
    CONNECTING,
    CONNECTED,
    DISCONNECTED,
    FAILED
};

struct WiFiScanResult
{
    String ssid;
    String bssid;
    int8_t rssi = 0;
    uint8_t channel = 0;
    wifi_auth_mode_t authMode = WIFI_AUTH_OPEN;
};
}

using WiFiConnectedCallback = void (*)(bool firstConnection);

void WiFi_onConnected(WiFiConnectedCallback callback);
bool WiFi_Connect(const char *ssid, const char *password, int timeoutMs = 0,
                  void *waitCallback(unsigned int) = nullptr);
bool WiFi_ConnectAsync(const char *ssid, const char *password,
                       bool deleteAfterConnect = true);
void WiFi_Disconnect();
bool WiFi_Auto();
void WiFi_Scan();
bool WiFi_ChangeCredentials(const String &ssid, const String &password);
bool WiFi_changeProfile(const NightMare::WiFiProfile &profile, bool force = false);
NightMare::WiFiProfile WiFi_getProfile();

bool WiFi_isConnected();
NightMare::WiFiStatus WiFi_status();
String WiFi_localIP();
String WiFi_currentSSID();
int WiFi_RSSI();

bool WiFi_startScan();
bool WiFi_scanInProgress();
int WiFi_scanCount();
bool WiFi_scanResult(size_t index, NightMare::WiFiScanResult &result);
void WiFi_clearScan();

const char *WiFi_getAuthTypeName(wifi_auth_mode_t authType);
const char *WiFi_getStatusName(NightMare::WiFiStatus status);
bool WiFi_isValidTxPower(int quarterDbm);
bool WiFi_setTxPower(int quarterDbm);
float WiFi_getTxPowerDbm();
bool WiFi_cancelAsyncConnect();
extern int gTxPower;

#endif // NM_ENABLE_WIFI
