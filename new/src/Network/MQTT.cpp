#include "MQTT.h"
#include "NmMqttEsp.h"
#include "NmMessageRouter.h"

#include <Core/DeviceIdentity.h>
#include <Core/ResourcesManager.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace
{
constexpr int QueueCapacity = 5;
struct QueuedMessage
{
    bool active = false;
    String topic;
    String payload;
    bool retained = false;
};

QueuedMessage pending[QueueCapacity];
SemaphoreHandle_t queueMutex = nullptr;
void (*projectMessage)(String, String) = nullptr;
void (*projectConnected)() = nullptr;
void (*projectDisconnected)(bool) = nullptr;
bool deviceMessagesOnly = true;

class MqttResourcePublisher : public ResourcePublisher
{
public:
    bool publish(const String &topic, const String &payload, bool retained) override
    {
        return MQTT_Publish(topic, payload, false, retained);
    }
};
MqttResourcePublisher resourcePublisher;

bool ensureQueueMutex()
{
    if (queueMutex == nullptr)
        queueMutex = xSemaphoreCreateMutex();
    return queueMutex != nullptr;
}

void flushQueuedMessages()
{
    if (queueMutex == nullptr)
        return;
    for (int i = 0; i < QueueCapacity; ++i)
    {
        xSemaphoreTake(queueMutex, portMAX_DELAY);
        if (!pending[i].active)
        {
            xSemaphoreGive(queueMutex);
            continue;
        }
        const String topic = pending[i].topic;
        const String payload = pending[i].payload;
        const bool retained = pending[i].retained;
        xSemaphoreGive(queueMutex);
        if (!NmMqttEsp::publish(topic, payload, retained))
            break;
        xSemaphoreTake(queueMutex, portMAX_DELAY);
        pending[i] = QueuedMessage();
        xSemaphoreGive(queueMutex);
    }
}

void messageReceived(const String &topic, const String &payload)
{
    if (NmMessageRouter::handleMessage(topic, payload))
        return;
    if (projectMessage == nullptr)
        return;
    if (!deviceMessagesOnly)
    {
        projectMessage(topic, payload);
        return;
    }
    String relative;
    if (gDeviceIdentity.relativeTopic(topic, relative))
        projectMessage(relative, payload);
}

void connected(bool)
{
    NmMessageRouter::onConnected();
    flushQueuedMessages();
    if (projectConnected != nullptr)
        projectConnected();
}

void disconnected(bool localBroker)
{
    if (projectDisconnected != nullptr)
        projectDisconnected(localBroker);
}
}

void MQTT_Init(bool localBroker)
{
    gDeviceIdentity.begin();
    gDeviceIdentity.lockAddress();
    gResourcesManager.setPublisher(&resourcePublisher);
    NmMqttEsp::setHandlers(messageReceived, connected, disconnected);
    NmMqttEsp::begin(localBroker);
}

void MQTT_End() { NmMqttEsp::end(); }
void MQTT_Finish()
{
    NmMqttEsp::finish();
    gResourcesManager.setPublisher(nullptr);
}
void MQTT_change_to(bool localBroker) { NmMqttEsp::changeTo(localBroker); }
bool MQTT_isLocal() { return NmMqttEsp::isLanBroker(); }
bool MQTT_Connected() { return NmMqttEsp::connected(); }
int8_t MQTT_State() { return NmMqttEsp::state(); }
String MQTTStateJson()
{
    String result = "{\"state\":";
    result += String(static_cast<int>(MQTT_State()));
    result += ",\"broker\":\"";
    result += MQTT_isLocal() ? "local" : "remote";
    result += "\"}";
    return result;
}

bool MQTT_Publish(const String &topic, const String &message, bool insertOwner, bool retained)
{
    const String fullTopic = insertOwner ? gDeviceIdentity.topic(topic) : topic;
    return NmMqttEsp::publish(fullTopic, message, retained);
}

void MQTT_Send(String topic, String message, bool insertOwner, bool retained)
{
    MQTT_Publish(topic, message, insertOwner, retained);
}

void MQTT_Send_Raw(String topic, String message)
{
    MQTT_Publish(topic, message, false, false);
}

bool MQTT_Queue_Async_Message(String topic, String message, bool insertOwner, bool retained)
{
    const String fullTopic = insertOwner ? gDeviceIdentity.topic(topic) : topic;
    if (NmMqttEsp::publish(fullTopic, message, retained))
        return true;
    if (!ensureQueueMutex())
        return false;
    xSemaphoreTake(queueMutex, portMAX_DELAY);
    for (int i = 0; i < QueueCapacity; ++i)
    {
        if (pending[i].active)
            continue;
        pending[i].topic = fullTopic;
        pending[i].payload = message;
        pending[i].retained = retained;
        pending[i].active = true;
        xSemaphoreGive(queueMutex);
        return true;
    }
    xSemaphoreGive(queueMutex);
    return false;
}

void MQTT_onMessage(void (*cb)(String, String), bool onlyDeviceMessages)
{
    projectMessage = cb;
    deviceMessagesOnly = onlyDeviceMessages;
}
void MQTT_onConnected(void (*cb)()) { projectConnected = cb; }
void MQTT_onDisconnected(void (*cb)(bool)) { projectDisconnected = cb; }
