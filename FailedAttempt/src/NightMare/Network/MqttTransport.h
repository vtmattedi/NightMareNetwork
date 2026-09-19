#pragma once
#include <NightMare/Network/Network.h>
#include <mqtt_client.h>
#include <atomic>

namespace NightMare {

class MqttTransport final : public Transport {
public:
    MqttTransport() = default;
    ~MqttTransport();
    bool attach(Network& network);
    // uri: mqtt://host:1883 or mqtts://host:8883. The certificate must outlive the client.
    bool begin(const char* uri, const char* user = nullptr, const char* password = nullptr,
               const char* certificate = nullptr);
    // Switch brokers after sustained disconnection. Call tick() on Runtime's
    // thread; no extra task is created. Credentials are shared by both brokers.
    bool configureFailover(const char* primaryUri, const char* backupUri,
                           uint32_t switchAfterMs = 30000);
    void tick(uint32_t nowMs = millis());
    bool usingBackup() const { return _usingBackup; }
    void end();
    bool connected() const override { return _connected.load(); }
    bool publish(const String& topic, const String& payload, bool retained = false) override;
private:
    static void eventHandler(void* argument, esp_event_base_t base, int32_t eventId, void* data);
    void handleEvent(int32_t eventId, esp_mqtt_event_handle_t event);
    Network* _network = nullptr;
    esp_mqtt_client_handle_t _client = nullptr;
    String _uri;
    String _primaryUri, _backupUri, _user, _password, _certificate;
    uint32_t _switchAfterMs = 30000;
    std::atomic<uint32_t> _disconnectedSinceMs{0};
    bool _usingBackup = false;
    bool _failover = false;
    bool _armed = false;
    String _clientId;
    String _statusTopic;
    String _consoleTopic;
    std::atomic<bool> _connected{false};
};

} // namespace NightMare
