#include <NightMare/Features.h>
#if NM_NETWORK_ESPNOW

#include "NmEspNowConnection.h"
#include "EspNowClient.h"

#include <Core/DeviceIdentity.h>
#include <Core/Logs.h>
#include <Network/NmMessageRouter.h>
#include <Network/NmConnectionInternal.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace
{
constexpr uint8_t QueueDepth = 8;
constexpr uint32_t WorkerStackBytes = 6144;

// Everything the client reports is handled on the worker: ingress work sends
// frames (restoring subscriptions, announcing status), and a send blocks until
// the send callback, which runs on the same task that delivers receive callbacks.
struct Event
{
    bool isMessage = false;
    NightMare::EspNowClient::State state = NightMare::EspNowClient::State::STOPPED;
    String topic;
    String payload;
};

QueueHandle_t events = nullptr;
TaskHandle_t worker = nullptr;
volatile bool workerRun = false;
volatile bool workerExited = true;
bool connectedReported = false;

void post(Event *event)
{
    if (events == nullptr || xQueueSend(events, &event, 0) != pdTRUE)
        delete event; // the client will resend state on the next change; messages are best effort
}

void onState(NightMare::EspNowClient::State state)
{
    Event *event = new Event();
    event->state = state;
    post(event);
}

void onMessage(const char *topic, const uint8_t *payload, size_t length, bool)
{
    Event *event = new Event();
    event->isMessage = true;
    event->topic = topic;
    event->payload.reserve(length);
    for (size_t i = 0; i < length; ++i)
        event->payload += static_cast<char>(payload[i]);
    post(event);
}

void handle(const Event &event)
{
    using NightMare::EspNowClient::State;
    if (!event.isMessage)
        LOG("ESPNOW", "Gateway state: %s",
            event.state == State::CONNECTED ? "connected"
            : event.state == State::SEARCHING ? "searching" : "stopped");
    if (event.isMessage)
    {
        NmMessageRouter::handleMessage(event.topic, event.payload);
        return;
    }
    if (event.state == State::CONNECTED)
    {
        connectedReported = true;
        NightMare::OnConnectedIngress(NightMare::ConnectionType::ESP_NOW);
    }
    else if (connectedReported)
    {
        connectedReported = false;
        NightMare::OnDisconnectedIngress(NightMare::ConnectionType::ESP_NOW);
    }
}

void workerTask(void *)
{
    while (workerRun)
    {
        Event *event = nullptr;
        if (xQueueReceive(events, &event, pdMS_TO_TICKS(200)) != pdTRUE)
            continue;
        handle(*event);
        delete event;
    }
    workerExited = true;
    worker = nullptr;
    vTaskDelete(nullptr);
}

void drain()
{
    Event *event = nullptr;
    while (events != nullptr && xQueueReceive(events, &event, 0) == pdTRUE)
        delete event;
}
}

namespace NmEspNowConnection
{
bool begin()
{
    if (workerRun)
        return true;

    gDeviceIdentity.begin();
    gDeviceIdentity.lockAddress();
    if (events == nullptr)
        events = xQueueCreate(QueueDepth, sizeof(Event *));
    if (events == nullptr)
        return false;
    drain();
    connectedReported = false;

    // The gateway publishes this when the device goes silent (300 s), the same
    // role the MQTT last will plays.
    const String willTopic = gDeviceIdentity.topic("status");
    const String offline = NightMare::ConnectionDeviceStatusJson(false);
    NightMare::EspNowClient::setLastWill(willTopic.c_str(),
                                         reinterpret_cast<const uint8_t *>(offline.c_str()),
                                         offline.length(), true);

    NightMare::EspNowClient::onState(onState);
    NightMare::EspNowClient::onMessage(onMessage);

    workerRun = true;
    workerExited = false;
    if (xTaskCreate(workerTask, "espnow_conn", WorkerStackBytes, nullptr, 3, &worker) != pdPASS)
    {
        workerRun = false;
        workerExited = true;
        return false;
    }
    if (!NightMare::EspNowClient::begin())
    {
        LOG_ERROR("ESPNOW", "Client did not start (see the ESPNOW log above)");
        end();
        return false;
    }
    LOG("ESPNOW", "Started; searching for the gateway");
    return true;
}

void end()
{
    NightMare::EspNowClient::end();
    NightMare::EspNowClient::onState(nullptr);
    NightMare::EspNowClient::onMessage(nullptr);
    workerRun = false;
    for (int i = 0; i < 100 && !workerExited; ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    drain();
}

bool running()
{
    return workerRun;
}

bool publish(const char *topic, const uint8_t *payload, size_t length, bool retained)
{
    return NightMare::EspNowClient::publish(topic, payload, length, retained);
}

bool subscribe(const char *topicFilter)
{
    return NightMare::EspNowClient::subscribe(topicFilter);
}

bool unsubscribe(const char *topicFilter)
{
    return NightMare::EspNowClient::unsubscribe(topicFilter);
}
}

#endif // NM_NETWORK_ESPNOW
