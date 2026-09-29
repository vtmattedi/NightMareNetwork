#pragma once

#include <NightMare/Features.h>
#if NM_ENABLE_NETWORK

#include <Arduino.h>
#include <Network/NmConnection.h>

namespace NightMare
{
// Driver-to-coordinator events. Not part of the application connection API.
void OnConnectedIngress(ConnectionType connection);
void OnDisconnectedIngress(ConnectionType connection);
void OnConnectionFailedIngress(ConnectionType connection);
// A link that MQTT-style connections need (Wi-Fi) came up or went down. Only
// reports availability; starting, selecting and failing over stay in NmConnection.
void OnLinkAvailabilityIngress(bool available);
bool PublishText(const String &topic, const String &payload, bool retained = false);
String ConnectionDeviceStatusJson(bool online);
String ConnectionDeviceStatusJson(const String &deviceName, bool online);
}

#endif // NM_ENABLE_NETWORK
