#pragma once
#include <NightMare/Network/Network.h>
#include <mqtt_client.h>

namespace NightMare {

class MqttTransport final : public Transport {
public:
    MqttTransport() = default;
    ~MqttTransport();
    bool attach(Network& network);
    // uri: mqtt://host:1883 or mqtts://host:8883. The certificate must outlive the client.
    bool begin(const char* uri, const char* user = nullptr, const char* password = nullptr,
               const char* certificate = nullptr);
    void end();
    bool connected() const override { return _connected; }
    bool publish(const String& topic, const String& payload, bool retained = false) override;
private:
    static void eventHandler(void* argument, esp_event_base_t base, int32_t eventId, void* data);
    void handleEvent(int32_t eventId, esp_mqtt_event_handle_t event);
    Network* _network = nullptr;
    esp_mqtt_client_handle_t _client = nullptr;
    String _uri;
    String _clientId;
    String _statusTopic;
    String _consoleTopic;
    volatile bool _connected = false;
};

} // namespace NightMare
