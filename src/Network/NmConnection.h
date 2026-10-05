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

bool Publish(const char *topic, const uint8_t *payload, size_t length,
             bool retained = false);
// Text convenience over Publish(): sends the String's bytes as the payload.
// Returns false when the active connection cannot publish right now.
bool PublishText(const String &topic, const String &payload, bool retained = false);
bool Subscribe(const char *topicFilter);
bool Unsubscribe(const char *topicFilter);
void OnMessage(MessageHandler handler);

bool SelectConnection(ConnectionType connection);

// Attaches framework routing/subscriptions and selects an already-usable active
// transport. It does not start or stop connectivity services.
bool ConnectionBegin();

ConnectionType GetActiveConnection();
ConnectionType GetPreferredConnection();
ConnectionState GetConnectionState();
const char *ConnectionTypeName(ConnectionType connection);
const char *MqttProfileName(MqttProfile profile);
const char *ConnectionStateName(ConnectionState state);
}

#endif // NM_ENABLE_NETWORK
