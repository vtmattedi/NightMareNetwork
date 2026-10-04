#include <NightMare/Features.h>
#if NM_ENABLE_WIFI

#include "NmWifiService.h"

#include <Network/WiFiRadio/NmWifiRadioService.h>
#include <Core/DeviceIdentity.h>
#include <Core/Logs.h>
#include <Core/PersistentKeys.h>
#include <Core/StateStore.h>
#if NM_ENABLE_NETWORK
#include <Network/NmConnectionInternal.h>
#endif
#if NM_ENABLE_MQTT
#include <Network/MQTT/NmMqttConnection.h>
#endif
#if NM_ENABLE_OTA
#include <Util/OTA.h>
#endif
#if NM_ENABLE_TIME_SYNC
#include <Util/TimeSyncronization.h>
#endif
#include <creds.h>

#if !defined(DEFAULT_SSID) || !defined(DEFAULT_PASSWORD)
#error "Please define DEFAULT_SSID and DEFAULT_PASSWORD in creds.h"
#endif

namespace
{
bool servicesStarted = false;
bool stationPrepared = false;
bool stationEnabled = false;
bool firstConnection = true;
WiFiConnectedCallback connectedCallback = nullptr;

bool saveProfile(const NightMare::WiFiProfile &profile)
{
    const String keys[] = {NightMare::PersistentKey::WifiSsid,
                           NightMare::PersistentKey::WifiPassword,
                           NightMare::PersistentKey::WifiTxPower};
    const String values[] = {profile.ssid.c_str(), profile.password.c_str(),
                             String(profile.txPower)};
    return PersistentSettings.setMany(keys, values, 3);
}

void startFrameworkServices()
{
    if (servicesStarted)
        return;
    servicesStarted = true;
#if NM_ENABLE_OTA
    initOTA();
#endif
#if NM_ENABLE_TIME_SYNC
    if (!startSntpTimeSync())
        LOG_ERROR("Time", "Could not start SNTP synchronization");
#endif
}

void onWiFiState(NightMare::WiFiState state)
{
#if NM_ENABLE_MQTT
    NmMqttConnection::onWiFiState(state == NightMare::WiFiState::CONNECTED);
#endif
#if NM_ENABLE_NETWORK
    NightMare::OnIpLinkAvailabilityIngress(state == NightMare::WiFiState::CONNECTED);
    NightMare::OnConnectivityStateChanged();
#endif
    if (state != NightMare::WiFiState::CONNECTED)
        return;
    startFrameworkServices();
    // The driver may have settled on a fallback TX power; keep it.
    const NightMare::WiFiProfile stored = NightMare::WiFiStoredProfile();
    const NightMare::WiFiInfo info = WiFi_info();
    if (info.txPower != stored.txPower)
    {
        NightMare::WiFiProfile updated = stored;
        updated.txPower = info.txPower;
        saveProfile(updated);
    }
    const bool wasFirst = firstConnection;
    firstConnection = false;
    if (connectedCallback != nullptr)
        connectedCallback(wasFirst);
}
}

namespace NightMare
{
WiFiProfile WiFiStoredProfile()
{
    PersistentSettings.begin();
    WiFiProfile profile;
    profile.ssid = PersistentSettings.getOrSave(PersistentKey::WifiSsid, DEFAULT_SSID).c_str();
    profile.password = PersistentSettings.getOrSave(PersistentKey::WifiPassword,
                                                     DEFAULT_PASSWORD).c_str();
    profile.txPower = PersistentSettings.getOrSave(PersistentKey::WifiTxPower,
                                                    String(NM_TX_POWER_AUTO)).toInt();
    return profile;
}

namespace
{
bool prepareStation()
{
    gDeviceIdentity.lockAddress();
    // Through the radio service, not straight to the driver, so the radio is
    // reported (ESP-NOW starts on it) even when the station is what woke it.
    if (!WiFiRadioBegin())
        return false;
    WiFi_onState(onWiFiState);
    stationPrepared = true;
    return true;
}
}

bool WiFiBegin()
{
    if (!prepareStation())
        return false;
    return WiFiIP_enable();
}

bool WiFiIP_enable()
{
    if (stationEnabled && WiFi_state() != WiFiState::STOPPED)
        return true;
    if (!stationPrepared && !prepareStation())
        return false;
    if (WiFiRadio_state() != ConnectivityState::READY)
        return false;
    stationEnabled = true;
    if (!WiFi_start(WiFiStoredProfile(), gDeviceIdentity.getDeviceName().c_str()))
    {
        stationEnabled = false;
#if NM_ENABLE_NETWORK
        OnConnectivityStateChanged();
#endif
        return false;
    }
#if NM_ENABLE_NETWORK
    OnConnectivityStateChanged();
#endif
    return true;
}

bool WiFiIP_disable()
{
#if NM_ENABLE_MQTT
    if (NmMqttConnection::enabled())
        return false;
#endif
    if (!stationEnabled)
        return true;
    stationEnabled = false;
    if (WiFi_state() != WiFiState::STOPPED)
        WiFi_stop();
#if NM_ENABLE_NETWORK
    OnConnectivityStateChanged();
#endif
    return true;
}

bool WiFiIP_enabled() { return stationEnabled; }

ConnectivityState WiFiIP_state()
{
    if (!stationEnabled)
        return ConnectivityState::STOPPED;
    switch (WiFi_state())
    {
    case WiFiState::STOPPED: return ConnectivityState::STARTING;
    case WiFiState::CONNECTING: return ConnectivityState::CONNECTING;
    case WiFiState::CONNECTED: return ConnectivityState::CONNECTED;
    case WiFiState::DISCONNECTED: return ConnectivityState::CONNECTING;
    }
    return ConnectivityState::ERROR;
}

bool WiFiApplyProfile(const WiFiProfile &profile)
{
    if (WiFi_state() == WiFiState::STOPPED)
        return WiFi_isValidTxPower(profile.txPower) && saveProfile(profile);
    return WiFi_changeProfile(profile) && saveProfile(profile);
}

void WiFiIP_tick()
{
    // Scan finalization lives in the driver so every completion, abort, and
    // timeout follows the same ESP-NOW resume path.
    WiFi_scanTick();
}

void WiFiIP_onRadioState(bool ready)
{
    if (!stationEnabled)
        return;
    if (!ready)
    {
        if (WiFi_state() != WiFiState::STOPPED)
            WiFi_stop();
        return;
    }
    if (WiFi_state() == WiFiState::STOPPED &&
        !WiFi_start(WiFiStoredProfile(), gDeviceIdentity.getDeviceName().c_str()))
    {
#if NM_ENABLE_NETWORK
        OnConnectivityStateChanged();
#endif
    }
}
}

void WiFi_onConnected(WiFiConnectedCallback callback) { connectedCallback = callback; }

#endif // NM_ENABLE_WIFI
