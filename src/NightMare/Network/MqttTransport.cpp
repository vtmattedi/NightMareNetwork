#include <NightMare/Network/MqttTransport.h>
#include <esp_system.h>

namespace NightMare {

static bool validSegment(const String& id) {
    if (!id.length() || id.length() > 64) return false;
    for (size_t i = 0; i < id.length(); ++i) {
        const char ch = id[i];
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) return false;
    }
    return true;
}

MqttTransport::~MqttTransport() { end(); }

bool MqttTransport::attach(Network& network) {
    if (_network && _network != &network) return false;
    _network = &network;
    return true;
}

bool MqttTransport::begin(const char* uri, const char* user, const char* password,
                          const char* certificate) {
    if (_client || !_network || !uri || !*uri ||
        !validSegment(_network->resources().device()) ||
        !validSegment(_network->resources().nameSpace())) return false;
    _uri = uri;
    if (_uri.startsWith("mqtts://") && (!certificate || !*certificate)) return false;
    _clientId = _network->resources().device() + "-" + String(esp_random(), HEX);
    _statusTopic = "nm/" + _network->resources().nameSpace() + "/" +
                   _network->resources().device() + "/status";
    _consoleTopic = _network->resources().device() + "/console/in";
    esp_mqtt_client_config_t config = {};
    config.broker.address.uri = _uri.c_str();
    if (certificate && *certificate) config.broker.verification.certificate = certificate;
    config.credentials.client_id = _clientId.c_str();
    config.credentials.username = user;
    config.credentials.authentication.password = password;
    config.session.last_will.topic = _statusTopic.c_str();
    config.session.last_will.msg = "offline";
    config.session.last_will.retain = 1;
    _client = esp_mqtt_client_init(&config);
    if (!_client) return false;
    esp_mqtt_client_register_event(_client, MQTT_EVENT_ANY, eventHandler, this);
    if (esp_mqtt_client_start(_client) != ESP_OK) { end(); return false; }
    return true;
}

void MqttTransport::end() {
    if (!_client) return;
    _connected = false;
    esp_mqtt_client_stop(_client);
    esp_mqtt_client_destroy(_client);
    _client = nullptr;
}

bool MqttTransport::publish(const String& topic, const String& payload, bool retained) {
    if (!_client || !_connected) return false;
    return esp_mqtt_client_publish(_client, topic.c_str(), payload.c_str(),
                                   payload.length(), 0, retained) >= 0;
}

void MqttTransport::eventHandler(void* argument, esp_event_base_t, int32_t eventId, void* data) {
    static_cast<MqttTransport*>(argument)->handleEvent(eventId, static_cast<esp_mqtt_event_handle_t>(data));
}

void MqttTransport::handleEvent(int32_t eventId, esp_mqtt_event_handle_t event) {
    switch (static_cast<esp_mqtt_event_id_t>(eventId)) {
    case MQTT_EVENT_CONNECTED:
        _connected = true;
        esp_mqtt_client_subscribe(_client, "nm/#", 0);
        esp_mqtt_client_subscribe(_client, "Control/time", 0);
        esp_mqtt_client_subscribe(_client, _consoleTopic.c_str(), 0);
        publish(_statusTopic, "online", true);
        if (_network) _network->onConnected();
        break;
    case MQTT_EVENT_DISCONNECTED:
        _connected = false;
        break;
    case MQTT_EVENT_DATA:
        if (_network && event->topic_len > 0 && event->topic_len <= 192 &&
            event->total_data_len <= 1024 && event->current_data_offset == 0 &&
            event->data_len == event->total_data_len) {
            _network->onMessage(String(event->topic, event->topic_len),
                                event->data_len ? String(event->data, event->data_len) : String());
        }
        break;
    default:
        break;
    }
}

} // namespace NightMare
