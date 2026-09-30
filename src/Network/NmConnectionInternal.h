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
// Two separate capabilities, which used to be one "link":
//   radio    the Wi-Fi driver is started -- enough for ESP-NOW
//   IP link  the station joined an AP and has an address -- needed by MQTT
// Both only report availability; starting, selecting and failing over stay in
// NmConnection.
void OnRadioAvailabilityIngress(bool available);
void OnIpLinkAvailabilityIngress(bool available);
bool PublishText(const String &topic, const String &payload, bool retained = false);
String ConnectionDeviceStatusJson(bool online);
String ConnectionDeviceStatusJson(const String &deviceName, bool online);
}

#endif // NM_ENABLE_NETWORK
