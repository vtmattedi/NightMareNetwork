#pragma once
#include <NightMare/Features.h>
#if NM_NETWORK_ESPNOW
#include <Arduino.h>
#include <Network/NmConnection.h>

// ESP-NOW-backed implementation used only by NmConnection. It adapts
// NightMare::EspNowClient (the gateway client) to the connection API and moves
// the client's callbacks off the ESP-NOW receive task, where blocking sends
// would deadlock against the send-complete callback.
namespace NmEspNowConnection
{
bool begin();
void end();

bool publish(const char *topic, const uint8_t *payload, size_t length,
             bool retained = false);
bool subscribe(const char *topicFilter);
bool unsubscribe(const char *topicFilter);
bool running();
}
#endif // NM_NETWORK_ESPNOW
