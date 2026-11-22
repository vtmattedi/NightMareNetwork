#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_MQTT
#include <Core/Text.h>
#include <Core/PlatformClock.h>
#include <Network/Connectivity.h>
#include <Network/NmConnection.h>

// MQTT service plus its driver adapter. NmConnection only routes over it when
// this independently enabled service is connected and holds presence.
namespace NmMqttConnection
{
bool publish(const char *topic, const uint8_t *payload, size_t length,
             bool retained = false);
bool subscribe(const char *topicFilter);
bool unsubscribe(const char *topicFilter);
int8_t state();
bool enabled();
NightMare::ConnectivityState connectivityState();
NightMare::MqttProfile profile();
void onWiFiState(bool connected);
// The transport or the preference changed: reconnect when the session's Last
// Will no longer matches whether MQTT may hold presence.
void onRoutingChanged();
// Connected with the status Last Will: usable as the NMNW transport.
bool presenceReady();

// MQTT-specific delivery queue used when a command response must survive a
// Wi-Fi/MQTT reconnect. This is intentionally not a generic connection API.
bool queueAsyncMessage(const std::string &topic, const std::string &message,
                       bool insertOwner = false, bool retained = false);
}

namespace NightMare
{
// Each enable/disable is saved (Configs nightmare:connection:mqtt_enabled and
// nightmare:connection:mqtt_profile) and restored by startNightMareESP().
bool Mqtt_enable(MqttProfile mqttProfile);
// Enables with the saved profile.
bool Mqtt_enable();
bool Mqtt_disable();
bool Mqtt_enabled();
ConnectivityState Mqtt_state();
MqttProfile Mqtt_profile();
// Whether creds.h describes that broker.
bool Mqtt_profileSupported(MqttProfile profile);
// The saved enable state: what startNightMareESP() restores.
bool Mqtt_savedEnabled();
}
#endif // NM_ENABLE_MQTT
