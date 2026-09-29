#include <NightMare/Features.h>
#if NM_ENABLE_WIFI

#include "NmWifiService.h"

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

#if !defined(DEFAULT_SSID) || !defined(DEFAULT_PASSWORD)
#error "Please define DEFAULT_SSID and DEFAULT_PASSWORD in creds.h"
#endif

namespace
{
bool servicesStarted = false;
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

void onWiFiState(NightMare::WiFiState state)
{
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

bool WiFiBegin()
{
    gDeviceIdentity.lockAddress();
    WiFi_onState(onWiFiState);
    return WiFi_start(WiFiStoredProfile(), gDeviceIdentity.getDeviceName().c_str());
}

bool WiFiApplyProfile(const WiFiProfile &profile)
{
    if (WiFi_state() == WiFiState::STOPPED)
        return WiFi_isValidTxPower(profile.txPower) && saveProfile(profile);
    return WiFi_changeProfile(profile) && saveProfile(profile);
}
}

void WiFi_onConnected(WiFiConnectedCallback callback) { connectedCallback = callback; }

#endif // NM_ENABLE_WIFI