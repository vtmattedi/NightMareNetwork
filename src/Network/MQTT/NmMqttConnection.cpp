#include <NightMare/Features.h>
#if NM_ENABLE_MQTT

#include "NmMqttConnection.h"
#include "NmMqttEsp.h"

#include <Core/DeviceIdentity.h>
#include <Network/NmMessageRouter.h>
#include <Network/NmConnectionInternal.h>
#include <Network/WiFiIP/NmWifiService.h>
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
volatile bool serviceEnabled = false;
volatile NightMare::ConnectivityState serviceState = NightMare::ConnectivityState::STOPPED;
volatile NightMare::ConnectionType serviceProfile =
#if NM_NETWORK_MQTT
    NightMare::ConnectionType::MQTT;
#else
    NightMare::ConnectionType::LOCAL_MQTT;
#endif

void serviceChanged()
{
    NightMare::OnConnectivityStateChanged();
}

bool mqttType(NightMare::ConnectionType type)
{
    return type == NightMare::ConnectionType::MQTT ||
           type == NightMare::ConnectionType::LOCAL_MQTT;
}

bool mqttProfileSupported(NightMare::ConnectionType type)
{
    if (type == NightMare::ConnectionType::MQTT)
        return NM_NETWORK_MQTT != 0;
    if (type == NightMare::ConnectionType::LOCAL_MQTT)
        return NM_NETWORK_LOCALMQTT != 0;
    return false;
}

bool localBroker(NightMare::ConnectionType type)
{
    return type == NightMare::ConnectionType::LOCAL_MQTT;
}

NightMare::ConnectionType connectionType(bool isLocalBroker)
{
    return isLocalBroker ? NightMare::ConnectionType::LOCAL_MQTT
                         : NightMare::ConnectionType::MQTT;
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

void messageReceived(const String &topic, const String &payload, bool retained)
{
    if (NightMare::GetActiveConnection() == serviceProfile)
        NmMessageRouter::handleMessage(topic, payload, retained);
}

void connected(bool isLocalBroker)
{
    if (!serviceEnabled)
        return;
    connectionErrors.store(0);
    const NightMare::ConnectionType type = connectionType(isLocalBroker);
    if (type != serviceProfile)
        return;
    serviceState = NightMare::ConnectivityState::CONNECTED;
    NightMare::OnConnectedIngress(type);
    if (NightMare::GetActiveConnection() == type &&
        NightMare::GetConnectionState() == NightMare::ConnectionState::CONNECTED)
        flushQueuedMessages();
    serviceChanged();
}

void disconnected(bool isLocalBroker)
{
    if (!serviceEnabled)
        return;
    serviceState = NightMare::WiFiIP_state() == NightMare::ConnectivityState::CONNECTED
                       ? NightMare::ConnectivityState::CONNECTING
                       : NightMare::ConnectivityState::STARTING;
    NightMare::OnDisconnectedIngress(connectionType(isLocalBroker));
    serviceChanged();
}

void connectionError(bool isLocalBroker)
{
    if (connectionErrors.fetch_add(1) + 1 < ErrorsBeforeSwitchFailure)
        return;
    connectionErrors.store(0);
    serviceState = NightMare::ConnectivityState::ERROR;
    NightMare::OnConnectionFailedIngress(connectionType(isLocalBroker));
    serviceChanged();
}
}

namespace NmMqttConnection
{
bool begin(NightMare::ConnectionType type)
{
    if (!mqttType(type))
        return false;

    gDeviceIdentity.begin();
    gDeviceIdentity.lockAddress();
    connectionErrors.store(0);
    NmMqttEsp::setHandlers(messageReceived, connected, disconnected, connectionError);
    return NmMqttEsp::begin(localBroker(type));
}

bool changeTo(NightMare::ConnectionType type)
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

bool enabled() { return serviceEnabled; }
NightMare::ConnectivityState connectivityState() { return serviceState; }
NightMare::ConnectionType profile() { return serviceProfile; }

void onWiFiState(bool connected)
{
    if (!serviceEnabled)
        return;
    if (!connected)
    {
        serviceState = NightMare::ConnectivityState::STARTING;
        NightMare::OnDisconnectedIngress(serviceProfile);
        serviceChanged();
        return;
    }
    serviceState = NightMare::ConnectivityState::CONNECTING;
    const bool accepted = state() == -1 ? begin(serviceProfile) : changeTo(serviceProfile);
    if (!accepted)
        serviceState = NightMare::ConnectivityState::ERROR;
    serviceChanged();
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

namespace NightMare
{
bool Mqtt_enable(ConnectionType mqttProfile)
{
    if (!mqttProfileSupported(mqttProfile) || !WiFiIP_enabled())
        return false;
    const bool profileChanged = serviceProfile != mqttProfile;
    serviceProfile = mqttProfile;
    if (serviceEnabled && !profileChanged)
        return true;
    serviceEnabled = true;
    serviceState = ConnectivityState::STARTING;
    if (WiFiIP_state() == ConnectivityState::CONNECTED)
    {
        serviceState = ConnectivityState::CONNECTING;
        const bool accepted = NmMqttConnection::state() == -1
                                  ? NmMqttConnection::begin(mqttProfile)
                                  : NmMqttConnection::changeTo(mqttProfile);
        if (!accepted)
        {
            serviceState = ConnectivityState::ERROR;
            serviceChanged();
            return false;
        }
    }
    serviceChanged();
    return true;
}

bool Mqtt_disable()
{
    if (!serviceEnabled)
        return true;
    const ConnectionType oldProfile = serviceProfile;
    serviceEnabled = false;
    serviceState = ConnectivityState::STOPPED;
    OnDisconnectedIngress(oldProfile);
    serviceChanged();
    NmMqttConnection::end();
    return true;
}

bool Mqtt_enabled() { return serviceEnabled; }
ConnectivityState Mqtt_state() { return serviceState; }
ConnectionType Mqtt_profile() { return serviceProfile; }
}

#endif // NM_ENABLE_MQTT
