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
enum class ConnectionType : uint8_t
{
    AUTO = 0,
    MQTT = 1,
    // 2 was the pre-freeze LOCAL_MQTT routing value. It is deliberately not
    // reused: local/remote is now an MQTT service profile, not a transport.
    ESP_NOW = 3
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

using MessageHandler = void (*)(const char *topic,
                                const uint8_t *payload,
                                size_t length,
                                bool retained);
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
// It runs directly on the task that caused the change (Wi-Fi monitor, MQTT or
// ESP-NOW task, or the application loop), so keep it short, never block, and guard any
// state it shares with the application. Do not start or stop network services from it.
using NetworkStatusHandler = void (*)(const NetworkStatusEvent &event);

bool Publish(const char *topic, const uint8_t *payload, size_t length,
             bool retained = false);
// Text convenience over Publish(): sends the std::string's bytes as the payload.
// Returns false when the active connection cannot publish right now.
bool PublishText(const std::string &topic, const std::string &payload, bool retained = false);
bool Subscribe(const char *topicFilter);
bool Unsubscribe(const char *topicFilter);
// Replaces the firmware connection handler. Register before ConnectionBegin();
// it runs after framework owners subscribe on every active-transport connect.
// Firmware-owned subscriptions belong in this callback because Subscribe()
// sends immediately and retains no reconnect state.
void OnConnect(ConnectHandler handler);
void OnMessage(MessageHandler handler);

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
