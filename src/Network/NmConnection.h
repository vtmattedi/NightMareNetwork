#pragma once

#include <NightMare/Features.h>
#if NM_ENABLE_NETWORK

#include <Core/Text.h>
#include <Core/PlatformClock.h>
#include <Core/Config.h>
#include <Network/Connectivity.h>

namespace NightMare
{
// Final public and persisted name. Values are stable configuration/wire IDs.
// Each value names one connection; the NMNW transport is whichever connection
// currently carries the framework's own traffic.
enum class ConnectionType : uint8_t
{
    AUTO = 0,
    MQTT = 1,
    // 2 was the pre-freeze LOCAL_MQTT routing value. It is deliberately not
    // reused: local/remote is now an MQTT service profile, not a transport.
    ESP_NOW = 3,
    // A gateway's own broker (NM_ENABLE_GATEWAY): the gateway as a device
    // publishes to its ESP-NOW subscribers and its MQTT uplink at once.
    GATEWAY = 4
};

enum class MqttProfile : uint8_t
{
    REMOTE = 0,
    LOCAL = 1
};

enum class ConnectionState : uint8_t
{
    STOPPED,
    DISCOVERING,
    CONNECTING,
    CONNECTED,
    ERROR
};

extern Config<int> preferredConnection;

// One received message. Lives on the caller's stack (or, with
// NM_MESSAGE_QUEUE_DEPTH, in the delivery queue until the handler returns):
// copy what you need.
struct ReceivedMessage
{
    const char *topic;
    const uint8_t *payload;
    size_t length;
    bool retained;
    // The connection it arrived on.
    ConnectionType source;
    // true: it arrived on the NMNW transport and no framework route consumed
    // it. false: it arrived on another connection through a SubscribeOn()
    // filter of that connection.
    bool transport;
};

using MessageHandler = void (*)(const ReceivedMessage &message);
using ConnectHandler = void (*)();

// The four layers whose state an application can follow, bottom to top. A layer
// that is not compiled in (see the NM_ENABLE_* / NM_NETWORK_* switches) never
// reports.
enum class NetworkLayer : uint8_t
{
    WIFI_RADIO, // the Wi-Fi driver is started; enough for ESP-NOW
    WIFI_IP,    // the station joined an AP and has an address
    MQTT,       // the MQTT service
    ESP_NOW     // the ESP-NOW service
};

// One state change of one layer. Lives on the caller's stack: copy what you need.
struct NetworkStatusEvent
{
    NetworkLayer layer;
    ConnectivityState state;    // the state it entered
    ConnectivityState previous; // the state it left
    // The layer is usable: the radio is READY, or the IP link / MQTT / ESP-NOW is CONNECTED.
    // "Off" is state == STOPPED; anything else that is not up is on its way (STARTING,
    // CONNECTING), suspended or failed (ERROR).
    bool up;
};

// Called once per layer state change; a layer that stays in its state is not reported.
// Without NM_MESSAGE_QUEUE_DEPTH it runs directly on the task that caused the change
// (Wi-Fi monitor, MQTT or ESP-NOW task, or the application loop), so keep it short,
// never block, and guard any state it shares with the application. Do not start or
// stop network services from it.
using NetworkStatusHandler = void (*)(const NetworkStatusEvent &event);

// ---------------------------------------------------------------------------
// NMNW transport: follows whichever connection carries the framework traffic.

bool Publish(const char *topic, const uint8_t *payload, size_t length,
             bool retained = false);
// Text convenience over Publish(): sends the std::string's bytes as the payload.
// Returns false when the active connection cannot publish right now.
bool PublishText(const std::string &topic, const std::string &payload, bool retained = false);
// Kept by the library and installed on the transport on every (re)connect and
// transport change. Subscribing twice to the same filter is one subscription;
// Unsubscribe() removes it. Returns false only for an invalid filter or a full
// table (NM_MAX_APP_SUBSCRIPTIONS); while no transport is up it is installed
// on the next connect.
bool Subscribe(const char *topicFilter);
bool Unsubscribe(const char *topicFilter);
// Runs after the framework and the Subscribe() filters are installed, on every
// connect of the transport and every transport change.
void OnConnect(ConnectHandler handler);
// One handler for everything the framework does not consume: transport
// messages and messages from SubscribeOn() filters of other connections.
void OnMessage(MessageHandler handler);

// ---------------------------------------------------------------------------
// Connections: each compiled connection, used directly whichever one is the
// transport. Nothing here changes routing or starts a service.

// The connection is connected (not necessarily the transport).
bool ConnectionUp(ConnectionType connection);
bool PublishOn(ConnectionType connection, const char *topic, const uint8_t *payload,
               size_t length, bool retained = false);
bool PublishTextOn(ConnectionType connection, const std::string &topic,
                   const std::string &payload, bool retained = false);
// Kept per connection and installed on every connect of that connection. A
// message matching one is delivered to OnMessage() with transport == false
// while that connection is not the transport.
bool SubscribeOn(ConnectionType connection, const char *topicFilter);
bool UnsubscribeOn(ConnectionType connection, const char *topicFilter);

// ---------------------------------------------------------------------------
// Delivery. With NM_MESSAGE_QUEUE_DEPTH > 0 the OnMessage, OnConnect and
// OnNetworkStatus handlers do not run on the network tasks: their events are
// queued and run by DeliverEvents(), which tickNightMareESP() calls unless
// NM_DELIVER_IN_TICK is 0. Without the queue DeliverEvents() does nothing.
// budgetMs 0 delivers everything queued; otherwise it stops once the budget
// is spent (at least one event is delivered).
void DeliverEvents(uint32_t budgetMs = 0);

struct DeliveryStats
{
    uint32_t queued;    // events accepted
    uint32_t delivered; // events handed to a handler
    uint32_t dropped;   // events lost to a full queue or no memory
    uint32_t highWater; // deepest the queue has been
};
DeliveryStats GetDeliveryStats();

bool SelectConnection(ConnectionType connection);

// Attaches framework routing and selects an already-usable active transport.
// It does not start or stop connectivity services.
bool ConnectionBegin();

ConnectionType GetActiveConnection();
ConnectionType GetPreferredConnection();
ConnectionState GetConnectionState();
const char *ConnectionTypeName(ConnectionType connection);
const char *MqttProfileName(MqttProfile profile);
const char *ConnectionStateName(ConnectionState state);
const char *NetworkLayerName(NetworkLayer layer);

// Installs the one network status handler; a later call replaces it, nullptr removes it.
// Layers already past STOPPED when it is installed report only their next change.
void OnNetworkStatus(NetworkStatusHandler handler);
}

#endif // NM_ENABLE_NETWORK
