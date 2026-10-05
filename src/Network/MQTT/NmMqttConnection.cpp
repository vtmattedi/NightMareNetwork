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
volatile NightMare::MqttProfile serviceProfile =
#if NM_NETWORK_MQTT
    NightMare::MqttProfile::REMOTE;
#else
    NightMare::MqttProfile::LOCAL;
#endif

void serviceChanged()
{
    NightMare::OnConnectivityStateChanged();
}

bool mqttProfileSupported(NightMare::MqttProfile profile)
{
    if (profile == NightMare::MqttProfile::REMOTE)
        return NM_NETWORK_MQTT != 0;
    if (profile == NightMare::MqttProfile::LOCAL)
        return NM_NETWORK_LOCALMQTT != 0;
    return false;
}

bool localBroker(NightMare::MqttProfile profile)
{
    return profile == NightMare::MqttProfile::LOCAL;
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
    if (NightMare::GetActiveConnection() == NightMare::ConnectionType::MQTT)
        NmMessageRouter::handleMessage(topic, payload, retained);
}

void connected(bool isLocalBroker)
{
    if (!serviceEnabled)
        return;
    connectionErrors.store(0);
    const NightMare::MqttProfile profile = isLocalBroker
                                               ? NightMare::MqttProfile::LOCAL
                                               : NightMare::MqttProfile::REMOTE;
    if (profile != serviceProfile)
        return;
    serviceState = NightMare::ConnectivityState::CONNECTED;
    NightMare::OnConnectedIngress(NightMare::ConnectionType::MQTT);
    if (NightMare::GetActiveConnection() == NightMare::ConnectionType::MQTT &&
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
    NightMare::OnDisconnectedIngress(NightMare::ConnectionType::MQTT);
    serviceChanged();
}

void connectionError(bool isLocalBroker)
{
    if (connectionErrors.fetch_add(1) + 1 < ErrorsBeforeSwitchFailure)
        return;
    connectionErrors.store(0);
    serviceState = NightMare::ConnectivityState::ERROR;
    NightMare::OnConnectionFailedIngress(NightMare::ConnectionType::MQTT);
    serviceChanged();
}
}

namespace NmMqttConnection
{
bool begin(NightMare::MqttProfile profile)
{
    if (!mqttProfileSupported(profile))
        return false;

    gDeviceIdentity.begin();
    gDeviceIdentity.lockAddress();
    connectionErrors.store(0);
    NmMqttEsp::setHandlers(messageReceived, connected, disconnected, connectionError);
    return NmMqttEsp::begin(localBroker(profile));
}

bool changeTo(NightMare::MqttProfile profile)
{
    if (!mqttProfileSupported(profile))
        return false;
    connectionErrors.store(0);
    return NmMqttEsp::changeTo(localBroker(profile));
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
NightMare::MqttProfile profile() { return serviceProfile; }

void onWiFiState(bool connected)
{
    if (!serviceEnabled)
        return;
    if (!connected)
    {
        serviceState = NightMare::ConnectivityState::STARTING;
        NightMare::OnDisconnectedIngress(NightMare::ConnectionType::MQTT);
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
bool Mqtt_enable(MqttProfile mqttProfile)
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
    serviceEnabled = false;
    serviceState = ConnectivityState::STOPPED;
    OnDisconnectedIngress(ConnectionType::MQTT);
    serviceChanged();
    NmMqttConnection::end();
    return true;
}

bool Mqtt_enabled() { return serviceEnabled; }
ConnectivityState Mqtt_state() { return serviceState; }
MqttProfile Mqtt_profile() { return serviceProfile; }
}

#endif // NM_ENABLE_MQTT
