#pragma once
#include <NightMare/Network/Dispatcher.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace NightMare {

class Console;

// MQTT callbacks enqueue bounded messages. All handlers and resource mutation
// run in tick() on the application's Runtime/loop thread.
class Network {
public:
    Network(const char* device, Transport& transport, const char* nameSpace = "default");
    ~Network();
    ResourceManager& resources() { return _resources; }
    void attachConsole(Console& console) { _console = &console; }
    bool hasConsole() const { return _console != nullptr; }
    bool connected() const { return _transport.connected(); }
    void onMessage(const String& topic, const String& payload);
    void onConnected();
    void tick(uint32_t nowMs = millis());
    uint32_t droppedMessages() const { return _dropped; }
private:
    struct Message { String topic; String payload; };
    static constexpr uint8_t QUEUE_SIZE = 8;
    Transport& _transport;
    ResourceManager _resources;
    Dispatcher _dispatcher;
    Console* _console = nullptr;
    SemaphoreHandle_t _mutex;
    Message _messages[QUEUE_SIZE];
    uint8_t _head = 0;
    uint8_t _tail = 0;
    uint8_t _count = 0;
    bool _reconnect = false;
    uint32_t _dropped = 0;
};

} // namespace NightMare
