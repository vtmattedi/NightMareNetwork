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
bool PublishText(const String &topic, const String &payload, bool retained = false);
String ConnectionDeviceStatusJson(bool online);
String ConnectionDeviceStatusJson(const String &deviceName, bool online);
}

#endif // NM_ENABLE_NETWORK
