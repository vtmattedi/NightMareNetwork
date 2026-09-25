#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_WIFI
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#if NM_ENABLE_OTA
#include <Util/OTA.h>
#endif

#include <Core/StateStore.h>
#include <creds.h> // supplied by the consuming project, not by this library
#if !defined(DEFAULT_SSID) || !defined(DEFAULT_PASSWORD)
#error "Please define DEFAULT_SSID and DEFAULT_PASSWORD in creds.h"
#endif

#if NM_ENABLE_TIME_SYNC
#include <Util/TimeSyncronization.h>
#endif

namespace NightMare
{
    /// Tx power in dBm. AUTO (0) means do not set it; the driver default stays.
    constexpr int NM_TX_POWER_AUTO = 0;
    struct WiFiProfile
    {
        String ssid;
        String password;
        int txPower = NM_TX_POWER_AUTO;
    };
}

typedef void (*WiFiConnectedCallback)(bool firstConnection);
void WiFi_onConnected(WiFiConnectedCallback callback);
bool WiFi_Connect(const char *ssid, const char *password, int timeoutMs = 0, void *waitCallback(unsigned int) = nullptr);
bool WiFi_ConnectAsync(const char *ssid, const char *password, bool deleteAfterConnect = true);
void WiFi_Disconnect();
bool WiFi_Auto();
void WiFi_Scan();
bool WiFi_ChangeCredentials(const String &ssid, const String &password);
const char *WiFi_getAuthTypeName(wifi_auth_mode_t authType);
const char *WiFi_getStatusName(wl_status_t status);
/// Connects with the profile (15 s timeout); persists it only on success, otherwise reverts.
bool WiFi_changeProfile(const NightMare::WiFiProfile &profile);
/// The stored profile (creds.h defaults / AUTO when nothing is stored).
NightMare::WiFiProfile WiFi_getProfile();
/// Sets and persists the tx power in dBm (or NM_TX_POWER_AUTO). Applied now unless AUTO.
bool WiFi_setTxPower(int txPowerDbm);
/// Live driver tx power in dBm, or NM_TX_POWER_AUTO if WiFi is off.
float WiFi_getTxPowerDbm();
#endif // NM_ENABLE_WIFI
