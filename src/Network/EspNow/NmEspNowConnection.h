#pragma once
#include <NightMare/Features.h>
#if NM_NETWORK_ESPNOW
#include <Arduino.h>
#include <Network/Connectivity.h>
#include <Network/NmConnection.h>

// Independently enabled ESP-NOW service and transport adapter. It adapts
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
bool enabled();
NightMare::ConnectivityState connectivityState();
bool suspend(NightMare::ConnectivitySuspendReason reason);
bool resume(NightMare::ConnectivitySuspendReason reason);
}

namespace NightMare
{
bool EspNow_enable();
bool EspNow_disable();
bool EspNow_enabled();
ConnectivityState EspNow_state();
bool EspNow_suspend(ConnectivitySuspendReason reason);
bool EspNow_resume(ConnectivitySuspendReason reason);
// Dependency fact from WiFiRadio; not an application lifecycle command.
void EspNow_onRadioState(bool ready);
}
#endif // NM_NETWORK_ESPNOW
