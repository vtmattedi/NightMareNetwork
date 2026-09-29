#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_MQTT
#include <Arduino.h>
#include <Network/NmConnection.h>

// MQTT-backed implementation used only by NmConnection. The NightMare-facing
// connection API remains Network/NmConnection.h.
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

// MQTT-specific delivery queue used when a command response must survive a
// Wi-Fi/MQTT reconnect. This is intentionally not a generic connection API.
bool queueAsyncMessage(const String &topic, const String &message,
                       bool insertOwner = false, bool retained = false);
}
#endif // NM_ENABLE_MQTT
