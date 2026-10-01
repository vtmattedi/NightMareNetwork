#pragma once

#include <NightMare/Features.h>
#if NM_ENABLE_NETWORK

#include <Arduino.h>
#include <Core/Config.h>

namespace NightMare
{
// Final public and persisted name. Values are stable configuration/wire IDs.
enum class ConnectionType : uint8_t
{
    AUTO = 0,
    MQTT,
    LOCAL_MQTT,
    ESP_NOW
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

bool Publish(const char *topic, const uint8_t *payload, size_t length,
             bool retained = false);
bool Subscribe(const char *topicFilter);
bool Unsubscribe(const char *topicFilter);
void OnMessage(MessageHandler handler);

bool SelectConnection(ConnectionType connection);

// Starts the preferred connection if what it runs on is already available:
// ESP-NOW needs the Wi-Fi radio, MQTT/LOCAL_MQTT an IP link. Otherwise it
// waits and starts from the radio/IP-link ingress. Called by
// startNightMareESP(); safe to call again.
bool ConnectionBegin();

ConnectionType GetSelectedConnection();
ConnectionState GetConnectionState();
}

#endif // NM_ENABLE_NETWORK
