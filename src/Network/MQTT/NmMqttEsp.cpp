#include <NightMare/Features.h>
#if NM_ENABLE_MQTT
#include "NmMqttEsp.h"
#include <Network/NmConnectionInternal.h>

#include <Core/DeviceIdentity.h>
#include <Core/Logs.h>
#include <creds.h>
#include <mqtt_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>

// Each broker profile exists when creds.h describes it: REMOTE needs
// REMOTE_MQTT_URL, REMOTE_MQTT_PORT and ROOT_CA (TLS), LOCAL needs
// LOCAL_MQTT_HOST and LOCAL_MQTT_PORT. MQTT_USER / MQTT_PASSWD are optional.
#if defined(REMOTE_MQTT_URL) && defined(REMOTE_MQTT_PORT) && defined(ROOT_CA)
#define NM_MQTT_HAS_REMOTE 1
#else
#define NM_MQTT_HAS_REMOTE 0
#endif
#if defined(LOCAL_MQTT_HOST) && defined(LOCAL_MQTT_PORT)
#define NM_MQTT_HAS_LOCAL 1
#else
#define NM_MQTT_HAS_LOCAL 0
#endif
#if !NM_MQTT_HAS_REMOTE && !NM_MQTT_HAS_LOCAL
#error "creds.h describes no MQTT broker: define REMOTE_MQTT_URL/REMOTE_MQTT_PORT/ROOT_CA or LOCAL_MQTT_HOST/LOCAL_MQTT_PORT"
#endif
#ifndef MQTT_USER
#define MQTT_USER nullptr
#endif
#ifndef MQTT_PASSWD
#define MQTT_PASSWD nullptr
#endif

namespace
{
enum class ControlAction : uint8_t { START, STOP, SHUTDOWN };
struct ControlCommand
{
    ControlAction action = ControlAction::STOP;
    bool lan = false;
    // START: install the status Last Will on the new session.
    bool will = false;
    // Publish the offline status before closing a session that holds presence.
    bool publishOffline = false;
};
constexpr size_t MaxIncomingLength = 32768;

QueueHandle_t controlQueue = nullptr;
TaskHandle_t controlTaskHandle = nullptr;
esp_mqtt_client_handle_t client = nullptr;
NmMqttEsp::MessageHandler onMessage = nullptr;
NmMqttEsp::ConnectedHandler onConnected = nullptr;
NmMqttEsp::DisconnectedHandler onDisconnected = nullptr;
NmMqttEsp::ErrorHandler onError = nullptr;
volatile int8_t connectionState = -1;
bool lanBroker = false;
// The current session carries the status Last Will.
volatile bool sessionWill = false;
std::string incomingTopic;
std::string incomingPayload;
bool incomingRetained = false;
bool receiving = false;
bool shuttingDown = false;
bool stopRequested = false;

// ESP MQTT retains pointers to these values for the client's lifetime.
char brokerUri[192] = {};
char willTopic[96] = {};
// The offline status JSON. Also what a graceful stop publishes, so the broker
// ends up with the same retained payload however the device went away.
char willMessage[512] = {};
char clientId[48] = {};
#if NM_MQTT_HAS_REMOTE
const char *rootCa = ROOT_CA;
#endif

// Only a session that holds presence speaks for <device>/status: closing one
// that does not (ESP-NOW carries the traffic) must not mark the device offline.
void stopClient(bool publishOffline)
{
    receiving = false;
    if (client != nullptr)
    {
        if (connectionState > 0 && sessionWill && publishOffline)
            esp_mqtt_client_publish(client, willTopic, willMessage,
                                    static_cast<int>(strlen(willMessage)), 0, true);
        esp_mqtt_client_stop(client);
        esp_mqtt_client_destroy(client);
        client = nullptr;
    }
    sessionWill = false;
    connectionState = -1;
}

void mqttEvent(void *, esp_event_base_t, int32_t eventId, void *eventData)
{
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(eventData);
    switch (static_cast<esp_mqtt_event_id_t>(eventId))
    {
    case MQTT_EVENT_CONNECTED:
        connectionState = lanBroker ? 1 : 2;
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
            incomingTopic = std::string(event->topic, event->topic_len);
            incomingPayload = std::string();
            incomingRetained = event->retain;
            if (event->total_data_len > 0 && !NightMare::Text::tryReserve(incomingPayload, event->total_data_len))
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
        if (event->data_len > 0)
            incomingPayload.append(event->data, static_cast<size_t>(event->data_len));
        if (incomingPayload.length() == static_cast<size_t>(event->total_data_len))
        {
            receiving = false;
            if (onMessage != nullptr)
                onMessage(incomingTopic, incomingPayload, incomingRetained);
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
        // The selected connection type is owned by NmConnection. ESP-IDF may
        // reconnect this broker, but the driver must never silently swap to
        // the other MQTT profile.
        LOG_WARNING("MQTT", "Broker connection error; retaining selected profile");
        if (onError != nullptr)
            onError(lanBroker);
        break;
    default:
        break;
    }
}

bool startClient(bool useLan, bool withWill)
{
    if (!NmMqttEsp::profileSupported(useLan))
    {
        LOG_ERROR("MQTT", "creds.h does not describe the %s broker", useLan ? "LAN" : "cloud");
        return false;
    }
    gDeviceIdentity.begin();
    gDeviceIdentity.lockAddress();
    lanBroker = useLan;
    snprintf(willTopic, sizeof(willTopic), "%s", gDeviceIdentity.topic("status").c_str());
    const std::string offlineStatus =
        NightMare::ConnectionDeviceStatusJson(false, NightMare::ConnectionType::MQTT);
    if (offlineStatus.length() >= sizeof(willMessage))
    {
        // Cannot happen with a valid name, but a cut-off JSON last will would be
        // worse than no connection attempt.
        LOG_ERROR("MQTT", "Offline status does not fit the last-will buffer");
        return false;
    }
    snprintf(willMessage, sizeof(willMessage), "%s", offlineStatus.c_str());
    snprintf(clientId, sizeof(clientId), "nm-%s", gDeviceIdentity.getDeviceId().c_str());
#if NM_MQTT_HAS_LOCAL
    if (useLan)
        snprintf(brokerUri, sizeof(brokerUri), "mqtt://%s:%d", LOCAL_MQTT_HOST, LOCAL_MQTT_PORT);
#endif
#if NM_MQTT_HAS_REMOTE
    if (!useLan)
        snprintf(brokerUri, sizeof(brokerUri), "mqtts://%s:%d", REMOTE_MQTT_URL, REMOTE_MQTT_PORT);
#endif

    esp_mqtt_client_config_t config = {};
    config.broker.address.uri = brokerUri;
#if NM_MQTT_HAS_REMOTE
    if (!useLan)
        config.broker.verification.certificate = rootCa;
#endif
    config.credentials.username = MQTT_USER;
    config.credentials.authentication.password = MQTT_PASSWD;
    config.credentials.client_id = clientId;
    // Without a Last Will the session says nothing about presence; the
    // connection that carries the traffic does.
    if (withWill)
    {
        config.session.last_will.topic = willTopic;
        config.session.last_will.msg = willMessage;
        config.session.last_will.qos = 0;
        config.session.last_will.retain = 1;
    }
    sessionWill = withWill;
    config.task.stack_size = 8192;
    config.task.priority = 5;

    LOG("MQTT", "Starting connection to %s broker at %s%s", useLan ? "LAN" : "cloud", brokerUri,
        withWill ? "" : " (no presence: another connection carries it)");

    client = esp_mqtt_client_init(&config);
    if (client == nullptr)
    {
        LOG_ERROR("MQTT", "Failed to initialize client for %s", brokerUri);
        sessionWill = false;
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
        sessionWill = false;
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
        stopClient(cmd.publishOffline);
        if (cmd.action == ControlAction::STOP)
            stopRequested = false;
        if (cmd.action == ControlAction::START)
            startClient(cmd.lan, cmd.will);
        if (cmd.action == ControlAction::SHUTDOWN)
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
                 DisconnectedHandler disconnectedHandler, ErrorHandler errorHandler)
{
    onMessage = message;
    onConnected = connectedHandler;
    onDisconnected = disconnectedHandler;
    onError = errorHandler;
}

bool profileSupported(bool useLan)
{
    return useLan ? NM_MQTT_HAS_LOCAL != 0 : NM_MQTT_HAS_REMOTE != 0;
}

bool begin(bool useLan, bool withWill)
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
    if (client != nullptr && lanBroker == useLan && sessionWill == withWill && !stopRequested)
        return true;
    return changeTo(useLan, withWill);
}

void end()
{
    if (controlQueue != nullptr)
    {
        ControlCommand cmd;
        cmd.action = ControlAction::STOP;
        cmd.publishOffline = true;
        if (xQueueSend(controlQueue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE)
            stopRequested = true;
    }
}

void finish()
{
    if (controlQueue == nullptr || shuttingDown)
        return;
    ControlCommand cmd;
    cmd.action = ControlAction::SHUTDOWN;
    cmd.publishOffline = true;
    if (xQueueSend(controlQueue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE)
        shuttingDown = true;
}

bool changeTo(bool useLan, bool withWill)
{
    if (shuttingDown)
        return false;
    if (controlQueue == nullptr)
        return begin(useLan, withWill);
    ControlCommand cmd;
    cmd.action = ControlAction::START;
    cmd.lan = useLan;
    cmd.will = withWill;
    // Leaving a broker that holds presence says goodbye there; changing only
    // the Last Will (presence moved to another connection) does not.
    cmd.publishOffline = useLan != lanBroker;
    return xQueueSend(controlQueue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
}

bool publish(const char *topic, const uint8_t *payload, size_t length, bool retained)
{
    if (client == nullptr || connectionState <= 0 || topic == nullptr || topic[0] == '\0' ||
        (payload == nullptr && length != 0) || length > static_cast<size_t>(INT_MAX))
        return false;
    return esp_mqtt_client_publish(client, topic,
                                   reinterpret_cast<const char *>(payload),
                                   static_cast<int>(length), 0, retained) >= 0;
}

bool subscribe(const char *topicFilter)
{
    const bool ok = client != nullptr && connectionState > 0 && topicFilter != nullptr &&
                    topicFilter[0] != '\0' &&
                    esp_mqtt_client_subscribe(client, topicFilter, 0) >= 0;
    LOG_DEBUG("MQTT", "Subscribe %s: %s", topicFilter == nullptr ? "" : topicFilter,
              OK_LOG(ok));
    return ok;
}

bool unsubscribe(const char *topicFilter)
{
    return client != nullptr && connectionState > 0 && topicFilter != nullptr &&
           topicFilter[0] != '\0' &&
           esp_mqtt_client_unsubscribe(client, topicFilter) >= 0;
}

bool connected() { return connectionState > 0; }
bool sessionHoldsPresence() { return connectionState > 0 && sessionWill; }
bool isLanBroker() { return lanBroker; }
int8_t state() { return connectionState; }
}
#endif // NM_ENABLE_MQTT
