#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_MQTT
#include <Arduino.h>
#include <Network/Connectivity.h>
#include <Network/NmConnection.h>

// MQTT service plus its driver adapter. NmConnection only routes over it when
// this independently enabled service is connected.
namespace NmMqttConnection
{
bool begin(NightMare::ConnectionType type);
bool changeTo(NightMare::ConnectionType type);
void end();
void finish();

bool publish(const char *topic, const uint8_t *payload, size_t length,
             bool retained = false);
bool subscribe(const char *topicFilter);
bool unsubscribe(const char *topicFilter);
int8_t state();
bool enabled();
NightMare::ConnectivityState connectivityState();
NightMare::ConnectionType profile();
void onWiFiState(bool connected);

// MQTT-specific delivery queue used when a command response must survive a
// Wi-Fi/MQTT reconnect. This is intentionally not a generic connection API.
bool queueAsyncMessage(const String &topic, const String &message,
                       bool insertOwner = false, bool retained = false);
}

namespace NightMare
{
bool Mqtt_enable(ConnectionType mqttProfile);
bool Mqtt_disable();
bool Mqtt_enabled();
ConnectivityState Mqtt_state();
ConnectionType Mqtt_profile();
}
#endif // NM_ENABLE_MQTT
