#include <NightMare/Features.h>
#if NM_ENABLE_MQTT
#include "NmMqttEsp.h"

#include <Core/DeviceIdentity.h>
#include <Core/Logs.h>
#include <creds.h>
#include <mqtt_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <errno.h>
#include <stdio.h>

#ifndef MQTT_CREDS_H
#error "Please create a creds.h file with the MQTT definitions."
#endif
#ifndef ROOT_CA
#error "Please define ROOT_CA in creds.h for the remote MQTT broker."
#endif

namespace
{
enum class ControlCommand : uint8_t { LAN, CLOUD, STOP, SHUTDOWN };
constexpr size_t MaxIncomingLength = 32768;
constexpr uint8_t MaxBrokerErrors = 1;

QueueHandle_t controlQueue = nullptr;
TaskHandle_t controlTaskHandle = nullptr;
esp_mqtt_client_handle_t client = nullptr;
NmMqttEsp::MessageHandler onMessage = nullptr;
NmMqttEsp::ConnectedHandler onConnected = nullptr;
NmMqttEsp::DisconnectedHandler onDisconnected = nullptr;
volatile int8_t connectionState = -1;
bool lanBroker = false;
uint8_t brokerErrors = 0;
String incomingTopic;
String incomingPayload;
bool receiving = false;
bool shuttingDown = false;
bool stopRequested = false;

// ESP MQTT retains pointers to these values for the client's lifetime.
char brokerUri[192] = {};
char willTopic[96] = {};
char clientId[48] = {};
const char *rootCa = ROOT_CA;

void stopClient()
{
    receiving = false;
    if (client != nullptr)
    {
        if (connectionState > 0)
            esp_mqtt_client_publish(client, willTopic, "offline", 7, 0, true);
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
        client = nullptr;
    }
    connectionState = -1;
}

void mqttEvent(void *, esp_event_base_t, int32_t eventId, void *eventData)
{
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(eventData);
    switch (static_cast<esp_mqtt_event_id_t>(eventId))
    {
    case MQTT_EVENT_CONNECTED:
        connectionState = lanBroker ? 1 : 2;
        brokerErrors = 0;
        LOG("MQTT", "Connected to %s broker (%s)", lanBroker ? "LAN" : "cloud", brokerUri);
        if (onConnected != nullptr)
            onConnected(lanBroker);
        break;
    case MQTT_EVENT_DISCONNECTED:
        connectionState = 0;
        receiving = false;
        if (onDisconnected != nullptr)
            onDisconnected(lanBroker);
        break;
    case MQTT_EVENT_DATA:
        if (event->current_data_offset == 0)
        {
            receiving = event->topic_len > 0 && event->total_data_len >= 0 &&
                        static_cast<size_t>(event->total_data_len) <= MaxIncomingLength;
            if (!receiving)
                break;
            incomingTopic = String(event->topic, event->topic_len);
            incomingPayload = String();
            if (event->total_data_len > 0 && !incomingPayload.reserve(event->total_data_len))
            {
                receiving = false;
                break;
            }
        }
        if (!receiving || event->current_data_offset != incomingPayload.length() ||
            event->data_len < 0 || incomingPayload.length() + event->data_len > MaxIncomingLength)
        {
            receiving = false;
            break;
        }
        if (event->data_len > 0 && !incomingPayload.concat(event->data, event->data_len))
        {
            receiving = false;
            break;
        }
        if (incomingPayload.length() == static_cast<size_t>(event->total_data_len))
        {
            receiving = false;
            if (onMessage != nullptr)
                onMessage(incomingTopic, incomingPayload);
        }
        break;
    case MQTT_EVENT_ERROR:
        if (event->error_handle != nullptr &&
            event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            const int err = event->error_handle->esp_transport_sock_errno;
            if (err == EAGAIN || err == EWOULDBLOCK || err == ENOMEM)
                break;
        }
        if (++brokerErrors > MaxBrokerErrors && controlQueue != nullptr)
        {
            const ControlCommand cmd = lanBroker ? ControlCommand::CLOUD : ControlCommand::LAN;
            xQueueSend(controlQueue, &cmd, 0);
            brokerErrors = 0;
        }
        break;
    default:
        break;
    }
}

bool startClient(bool useLan)
{
    gDeviceIdentity.begin();
    gDeviceIdentity.lockAddress();
    lanBroker = useLan;
    snprintf(willTopic, sizeof(willTopic), "%s", gDeviceIdentity.topic("status").c_str());
    snprintf(clientId, sizeof(clientId), "nm-%s", gDeviceIdentity.getDeviceId().c_str());
    if (useLan)
        snprintf(brokerUri, sizeof(brokerUri), "mqtt://%s:%d", LOCAL_MQTT_HOST, LOCAL_MQTT_PORT);
    else
        snprintf(brokerUri, sizeof(brokerUri), "mqtts://%s:%d", REMOTE_MQTT_URL, REMOTE_MQTT_PORT);

    esp_mqtt_client_config_t config = {};
    config.broker.address.uri = brokerUri;
    if (!useLan)
        config.broker.verification.certificate = rootCa;
    config.credentials.username = MQTT_USER;
    config.credentials.authentication.password = MQTT_PASSWD;
    config.credentials.client_id = clientId;
    config.session.last_will.topic = willTopic;
    config.session.last_will.msg = "offline";
    config.session.last_will.qos = 0;
    config.session.last_will.retain = 1;
    config.task.stack_size = 8192;
    config.task.priority = 5;

    LOG("MQTT", "Starting connection to %s broker at %s", useLan ? "LAN" : "cloud", brokerUri);

    client = esp_mqtt_client_init(&config);
    if (client == nullptr)
    {
        LOG_ERROR("MQTT", "Failed to initialize client for %s", brokerUri);
        return false;
    }
    esp_mqtt_client_register_event(client, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),
                                   mqttEvent, nullptr);
    connectionState = -2;
    if (esp_mqtt_client_start(client) != ESP_OK)
    {
        LOG_ERROR("MQTT", "Failed to start client for %s", brokerUri);
        esp_mqtt_client_destroy(client);
        client = nullptr;
        connectionState = -1;
        return false;
    }
    return true;
}

void controlTask(void *)
{
    ControlCommand cmd;
    while (xQueueReceive(controlQueue, &cmd, portMAX_DELAY) == pdTRUE)
    {
        stopClient();
        if (cmd == ControlCommand::STOP)
            stopRequested = false;
        if (cmd == ControlCommand::LAN || cmd == ControlCommand::CLOUD)
        {
            brokerErrors = 0;
            startClient(cmd == ControlCommand::LAN);
        }
        if (cmd == ControlCommand::SHUTDOWN)
        {
            QueueHandle_t completedQueue = controlQueue;
            controlQueue = nullptr;
            controlTaskHandle = nullptr;
            vQueueDelete(completedQueue);
            shuttingDown = false;
            vTaskDelete(nullptr);
        }
    }
    vTaskDelete(nullptr);
}
}

namespace NmMqttEsp
{
void setHandlers(MessageHandler message, ConnectedHandler connectedHandler,
                 DisconnectedHandler disconnectedHandler)
{
    onMessage = message;
    onConnected = connectedHandler;
    onDisconnected = disconnectedHandler;
}

bool begin(bool useLan)
{
    if (shuttingDown)
        return false;
    if (controlQueue == nullptr)
    {
        controlQueue = xQueueCreate(4, sizeof(ControlCommand));
        if (controlQueue == nullptr || xTaskCreate(controlTask, "mqtt_ctrl", 4096,
                                                   nullptr, 5, &controlTaskHandle) != pdPASS)
        {
            if (controlQueue != nullptr)
                vQueueDelete(controlQueue);
            controlQueue = nullptr;
            return false;
        }
    }
    if (client != nullptr && lanBroker == useLan && !stopRequested)
        return true;
    return changeTo(useLan);
}

void end()
{
    if (controlQueue != nullptr)
    {
        const ControlCommand cmd = ControlCommand::STOP;
        if (xQueueSend(controlQueue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE)
            stopRequested = true;
    }
}

void finish()
{
    if (controlQueue == nullptr || shuttingDown)
        return;
    const ControlCommand cmd = ControlCommand::SHUTDOWN;
    if (xQueueSend(controlQueue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE)
        shuttingDown = true;
}

bool changeTo(bool useLan)
{
    if (shuttingDown)
        return false;
    if (controlQueue == nullptr)
        return begin(useLan);
    const ControlCommand cmd = useLan ? ControlCommand::LAN : ControlCommand::CLOUD;
    return xQueueSend(controlQueue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool publish(const String &topic, const String &payload, bool retained)
{
    if (client == nullptr || connectionState <= 0 || topic.length() == 0)
        return false;
    return esp_mqtt_client_publish(client, topic.c_str(), payload.c_str(),
                                   payload.length(), 0, retained) >= 0;
}

bool subscribe(const String &topicFilter)
{
    const bool ok = client != nullptr && connectionState > 0 && topicFilter.length() != 0 &&
                    esp_mqtt_client_subscribe(client, topicFilter.c_str(), 0) >= 0;
    LOG_DEBUG("MQTT", "Subscribe %s: %s", topicFilter.c_str(), OK_LOG(ok));
    return ok;
}

bool unsubscribe(const String &topicFilter)
{
    return client != nullptr && connectionState > 0 && topicFilter.length() != 0 &&
           esp_mqtt_client_unsubscribe(client, topicFilter.c_str()) >= 0;
}

bool connected() { return connectionState > 0; }
bool isLanBroker() { return lanBroker; }
int8_t state() { return connectionState; }
}
#endif // NM_ENABLE_MQTT
