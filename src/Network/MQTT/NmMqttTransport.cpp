#include <NightMare/Features.h>
#if NM_ENABLE_MQTT

#include "NmMqttTransport.h"
#include "NmMqttEsp.h"

#include <Core/DeviceIdentity.h>
#include <Network/NmMessageRouter.h>
#include <Network/NmTransportInternal.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <atomic>

namespace
{
constexpr int QueueCapacity = 5;
constexpr uint8_t ErrorsBeforeSwitchFailure = 2;

struct QueuedMessage
{
    bool active = false;
    String topic;
    String payload;
    bool retained = false;
};

QueuedMessage pending[QueueCapacity];
SemaphoreHandle_t queueMutex = nullptr;
std::atomic<uint8_t> connectionErrors{0};

bool mqttType(NightMare::TransportType type)
{
    return type == NightMare::TransportType::MQTT ||
           type == NightMare::TransportType::LOCAL_MQTT;
}

bool localBroker(NightMare::TransportType type)
{
    return type == NightMare::TransportType::LOCAL_MQTT;
}

NightMare::TransportType connectionType(bool isLocalBroker)
{
    return isLocalBroker ? NightMare::TransportType::LOCAL_MQTT
                         : NightMare::TransportType::MQTT;
}

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

        if (!NmMqttEsp::publish(
                topic.c_str(),
                reinterpret_cast<const uint8_t *>(payload.c_str()),
                payload.length(), retained))
            break;

        xSemaphoreTake(queueMutex, portMAX_DELAY);
        pending[i] = QueuedMessage();
        xSemaphoreGive(queueMutex);
    }
}

void messageReceived(const String &topic, const String &payload)
{
    NmMessageRouter::handleMessage(topic, payload);
}

void connected(bool isLocalBroker)
{
    connectionErrors.store(0);
    const NightMare::TransportType type = connectionType(isLocalBroker);
    NightMare::TransportConnectedIngress(type);
    if (NightMare::GetSelectedTransport() == type &&
        NightMare::GetTransportState() == NightMare::TransportState::CONNECTED)
        flushQueuedMessages();
}

void disconnected(bool isLocalBroker)
{
    NightMare::TransportDisconnectedIngress(connectionType(isLocalBroker));
}

void connectionError(bool isLocalBroker)
{
    if (connectionErrors.fetch_add(1) + 1 < ErrorsBeforeSwitchFailure)
        return;
    connectionErrors.store(0);
    NightMare::TransportConnectionFailedIngress(connectionType(isLocalBroker));
}
}

namespace NmMqttTransport
{
bool begin(NightMare::TransportType type)
{
    if (!mqttType(type))
        return false;

    gDeviceIdentity.begin();
    gDeviceIdentity.lockAddress();
    connectionErrors.store(0);
    NmMqttEsp::setHandlers(messageReceived, connected, disconnected, connectionError);
    return NmMqttEsp::begin(localBroker(type));
}

bool changeTo(NightMare::TransportType type)
{
    if (!mqttType(type))
        return false;
    connectionErrors.store(0);
    return NmMqttEsp::changeTo(localBroker(type));
}

void end()
{
    NmMqttEsp::end();
}

void finish()
{
    NmMqttEsp::finish();
}

bool publish(const char *topic, const uint8_t *payload, size_t length, bool retained)
{
    return NmMqttEsp::publish(topic, payload, length, retained);
}

bool subscribe(const char *topicFilter)
{
    return NmMqttEsp::subscribe(topicFilter);
}

bool unsubscribe(const char *topicFilter)
{
    return NmMqttEsp::unsubscribe(topicFilter);
}

int8_t state()
{
    return NmMqttEsp::state();
}

bool queueAsyncMessage(const String &topic, const String &message,
                       bool insertOwner, bool retained)
{
    const String fullTopic = insertOwner ? gDeviceIdentity.topic(topic) : topic;
    if (publish(fullTopic.c_str(),
                reinterpret_cast<const uint8_t *>(message.c_str()),
                message.length(), retained))
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
}

#endif // NM_ENABLE_MQTT
