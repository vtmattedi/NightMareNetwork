#pragma once
#include "Config.h"
extern Config<int> preferredTransport("nightmare:connection:prefred_transport", static_cast<int>(NightMare::TransportType::AUTO), true);
namespace NightMare
{
    enum class TransportType
    {
        AUTO = 0, // Reserved for prefered transport selection
        MQTT,
        LOCAL_MQTT,
        ESP_NOW
    };

    void Publish(const char *topic, const char *payload, bool retained = false);
    void Subscribe(const char *topicFilter, void (*callback)(const char *topicFilter, const uint8_t *payload, size_t len));
    void Unsubscribe(const char *topicFilter);
    void OnMessage(const char *topic, const char *payload);
    void OnConnect();
    void SelectTransport(const char *transport);
    TransportType GetSelectedTransport();
}