#include <NightMare/Network/Network.h>
#include <NightMare/Core/Time.h>
#include <NightMare/Network/Console.h>
#include <ArduinoJson.h>

namespace NightMare {

Network::Network(const char* device, Transport& transport, const char* nameSpace)
    : _transport(transport), _resources(device, transport, nameSpace),
      _dispatcher(_resources), _mutex(xSemaphoreCreateMutex()) {}

Network::~Network() { if (_mutex) vSemaphoreDelete(_mutex); }

void Network::onMessage(const String& topic, const String& payload) {
    if (topic != "Control/time" && topic != _resources.device() + "/console/in" &&
        !topic.startsWith("nm/" + _resources.nameSpace() + "/")) return;
    if (topic.endsWith("/schema") || topic.endsWith("/status")) return;
    String ownResources = "nm/" + _resources.nameSpace() + "/" + _resources.device() + "/r/";
    if (topic.startsWith(ownResources) && (topic.endsWith("/state") || topic.endsWith("/emit"))) return;
    if (!_mutex || xSemaphoreTake(_mutex, 0) != pdTRUE) { ++_dropped; return; }
    if (_count == QUEUE_SIZE) ++_dropped;
    else {
        _messages[_tail].topic = topic;
        _messages[_tail].payload = payload;
        _tail = (_tail + 1) % QUEUE_SIZE;
        ++_count;
    }
    xSemaphoreGive(_mutex);
}

void Network::onConnected() {
    if (_mutex && xSemaphoreTake(_mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        _reconnect = true;
        xSemaphoreGive(_mutex);
    }
}

void Network::tick(uint32_t nowMs) {
    if (!_mutex) return;
    bool reconnect = false;
    if (xSemaphoreTake(_mutex, 0) == pdTRUE) {
        reconnect = _reconnect;
        _reconnect = false;
        xSemaphoreGive(_mutex);
    }
    if (reconnect && _transport.connected()) {
        _resources.connected();
        if (!Time::valid()) _transport.publish("Control/request", "time");
    }
    for (uint8_t processed = 0; processed < QUEUE_SIZE; ++processed) {
        Message message;
        if (xSemaphoreTake(_mutex, 0) != pdTRUE) break;
        if (!_count) { xSemaphoreGive(_mutex); break; }
        message = _messages[_head];
        _messages[_head] = {};
        _head = (_head + 1) % QUEUE_SIZE;
        --_count;
        xSemaphoreGive(_mutex);
        if (message.topic == "Control/time") {
            StaticJsonDocument<256> doc;
            if (!deserializeJson(doc, message.payload) && doc.containsKey("timestamp")) {
                double raw = doc["timestamp"].as<double>();
                if (raw > 4102444800.0 && raw <= 4102444800000.0) raw /= 1000.0;
                if (raw >= 1577836800.0 && raw <= 4102444800.0)
                    Time::setEpoch(static_cast<time_t>(raw));
                // Offset is presentation data; the system clock remains UTC.
            }
        } else if (message.topic == _resources.device() + "/console/in") {
            Console(_resources).execute(message.payload);
        } else _dispatcher.dispatch(message.topic, message.payload);
    }
    _resources.tick(nowMs);
}

} // namespace NightMare
