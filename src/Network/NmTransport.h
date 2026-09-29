#pragma once

#include <NightMare/Features.h>
#if NM_ENABLE_NETWORK

#include <Arduino.h>
#include <Core/Config.h>

namespace NightMare
{
enum class TransportType : uint8_t
{
    AUTO = 0,
    MQTT,
    LOCAL_MQTT,
    ESP_NOW
};

enum class TransportState : uint8_t
{
    STOPPED,
    DISCOVERING,
    CONNECTING,
    CONNECTED,
    ERROR
};

extern Config<int> preferredTransport;

bool Publish(const char *topic, const uint8_t *payload, size_t length,
             bool retained = false);
bool Subscribe(const char *topicFilter);
bool Unsubscribe(const char *topicFilter);

bool SelectTransport(TransportType transport);

TransportType GetSelectedTransport();
TransportState GetTransportState();
}

#endif // NM_ENABLE_NETWORK
