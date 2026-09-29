#pragma once

#include <NightMare/Features.h>
#if NM_ENABLE_NETWORK

#include <Arduino.h>
#include <Network/NmTransport.h>

namespace NightMare
{
// Driver-to-coordinator events. Not part of the application transport API.
void TransportConnectedIngress(TransportType transport);
void TransportDisconnectedIngress(TransportType transport);
void TransportConnectionFailedIngress(TransportType transport);
bool PublishText(const String &topic, const String &payload, bool retained = false);
bool PublishDeviceText(const String &topic, const String &payload, bool retained = false);
String TransportDeviceStatusJson(bool online);
String TransportDeviceStatusJson(const String &deviceName, bool online);
}

#endif // NM_ENABLE_NETWORK
