#pragma once

#include <NightMare/Features.h>
#if NM_ENABLE_NETWORK

#include <Core/Text.h>
#include <Core/PlatformClock.h>
#include <Network/NmConnection.h>

namespace NightMare
{
// Driver-to-coordinator events. Not part of the application connection API.
// A connection reports its own sessions; OnConnectedIngress also reinstalls
// that connection's SubscribeOn() filters.
void OnConnectedIngress(ConnectionType connection);
void OnDisconnectedIngress(ConnectionType connection);
void OnConnectionFailedIngress(ConnectionType connection);
// Every message a connection receives. The transport's go to the framework
// routes; another connection's reach the application only through its
// SubscribeOn() filters.
void OnMessageIngress(ConnectionType source, const std::string &topic,
                      const std::string &payload, bool retained);
// Reconsiders routing among already-enabled, connected services and, with a
// delivery queue, runs queued application events. Called from
// tickNightMareESP(); it never starts or stops a connectivity service.
void ConnectionTick();
// Two separate capabilities, which used to be one "link":
//   radio    the Wi-Fi driver is started -- enough for ESP-NOW
//   IP link  the station joined an AP and has an address -- needed by MQTT
// Both only report availability; their owning services control lifecycle.
void OnRadioAvailabilityIngress(bool available);
void OnIpLinkAvailabilityIngress(bool available);
// Any connectivity-service transition asks telemetry to refresh and makes the
// router reconsider which already-connected service should be active.
void OnConnectivityStateChanged();
// Framework-owned subscriptions: sent to the current transport and not
// remembered. Each owner installs its filters again from the connected event.
bool TransportSubscribe(const char *topicFilter);
bool TransportUnsubscribe(const char *topicFilter);
// Whether MQTT should carry the device's status Last Will right now. False
// only while ESP-NOW is the transport and is going to stay it.
bool MqttMayHoldPresence();
// MQTT only: a message received outside the namespace prefix, with its absolute topic.
// Delivered to the application when it matches a SubscribeGlobal() filter, otherwise
// dropped and counted (TopicNamespace::foreignDroppedCount()). It never reaches the
// framework routes.
void OnGlobalMessageIngress(const std::string &topic, const std::string &payload, bool retained);
#if NM_ENABLE_GATEWAY
// A message the gateway's broker received from either side, offered to the
// device stack as GATEWAY-connection ingress. `consumed` is set when a
// framework route acted on it (a request to this device, the broadcast
// console, Control/time); the application's handler does not count.
void OnGatewayIngress(const std::string &topic, const std::string &payload, bool retained,
                      bool &consumed);
#endif
// Called by NmMessageRouter only after framework-owned routes decline a
// message. Returns whether an application handler was installed.
bool DispatchApplicationMessage(const char *topic, const uint8_t *payload,
                                size_t length, bool retained);
// Publishes now if it can, otherwise holds the message (a few slots) until the
// next OnConnectedIngress of any connection -- e.g. a reply to a command that
// is about to drop the link. False only when the holding slots are full.
bool PublishTextWhenConnected(const std::string &topic, const std::string &payload, bool retained = false);
// The <device>/status document. `via` names the connection that carries it;
// AUTO leaves the route out. The one-argument form uses the transport.
std::string ConnectionDeviceStatusJson(bool online);
std::string ConnectionDeviceStatusJson(bool online, ConnectionType via);
std::string ConnectionDeviceStatusJson(const std::string &deviceName, bool online,
                                       ConnectionType via);
}

#endif // NM_ENABLE_NETWORK
