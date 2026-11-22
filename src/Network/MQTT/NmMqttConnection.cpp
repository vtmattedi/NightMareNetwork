#include <NightMare/Features.h>
#if NM_ENABLE_MQTT

#include "NmMqttConnection.h"
#include "NmMqttEsp.h"

#include <Core/Config.h>
#include <Core/DeviceIdentity.h>
#include <Core/Logs.h>
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
    std::string topic;
    std::string payload;
    bool retained = false;
};

QueuedMessage pending[QueueCapacity];
SemaphoreHandle_t queueMutex = nullptr;
std::atomic<uint8_t> connectionErrors{0};
volatile bool serviceEnabled = false;
volatile NightMare::ConnectivityState serviceState = NightMare::ConnectivityState::STOPPED;
volatile NightMare::MqttProfile serviceProfile = NightMare::MqttProfile::REMOTE;
// The Last Will last asked of the driver; a mismatch with
// MqttMayHoldPresence() asks for a reconnect.
volatile bool requestedWill = false;

bool localBroker(NightMare::MqttProfile profile)
{
    return profile == NightMare::MqttProfile::LOCAL;
}

bool profileSupported(NightMare::MqttProfile profile)
{
    return (profile == NightMare::MqttProfile::REMOTE || profile == NightMare::MqttProfile::LOCAL) &&
           NmMqttEsp::profileSupported(localBroker(profile));
}

NightMare::MqttProfile supportedProfile(NightMare::MqttProfile wanted)
{
    if (profileSupported(wanted))
        return wanted;
    return wanted == NightMare::MqttProfile::REMOTE ? NightMare::MqttProfile::LOCAL
                                                    : NightMare::MqttProfile::REMOTE;
}

bool applyEnable(NightMare::MqttProfile mqttProfile);
bool applyDisable();

bool writeEnabled(Config<bool> &, const bool &requested)
{
    return requested ? applyEnable(serviceProfile) : applyDisable();
}

bool writeProfile(Config<int> &, const int &requested)
{
    const NightMare::MqttProfile profile = static_cast<NightMare::MqttProfile>(requested);
    if (!profileSupported(profile))
        return false;
    if (!serviceEnabled)
    {
        serviceProfile = profile;
        return true;
    }
    return applyEnable(profile);
}
}

//@NightMare:Config MQTT enabled
// Saved by Mqtt_enable()/Mqtt_disable() and restored at startup. A CONFIG SET
// applies it at once.
static Config<bool> mqttEnabledSetting("nightmare:connection:mqtt_enabled", NM_MQTT_AUTO != 0);
//@NightMare:Config MQTT profile
// 0 REMOTE, 1 LOCAL. Falls back to the other one when creds.h does not
// describe the saved broker.
static Config<int> mqttProfileSetting("nightmare:connection:mqtt_profile",
                                      static_cast<int>(NightMare::MqttProfile::REMOTE));

namespace
{
struct SettingHandlerInstaller
{
    SettingHandlerInstaller()
    {
        mqttEnabledSetting.onWrite = writeEnabled;
        mqttProfileSetting.onWrite = writeProfile;
    }
};
SettingHandlerInstaller settingHandlerInstaller;

void saveSettings(bool enabled, NightMare::MqttProfile profile)
{
    if (mqttEnabledSetting.value() != enabled && !mqttEnabledSetting.set(enabled))
        LOG_WARNING("MQTT", "Could not save the MQTT enable state");
    if (enabled && mqttProfileSetting.value() != static_cast<int>(profile) &&
        !mqttProfileSetting.set(static_cast<int>(profile)))
        LOG_WARNING("MQTT", "Could not save the MQTT profile");
}

void serviceChanged()
{
    NightMare::OnConnectivityStateChanged();
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
        const std::string topic = pending[i].topic;
        const std::string payload = pending[i].payload;
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

// Starts or restarts the client for the current profile, with the Last Will
// that the current routing calls for.
bool startDriver(NightMare::MqttProfile profile)
{
    gDeviceIdentity.begin();
    gDeviceIdentity.lockAddress();
    connectionErrors.store(0);
    const bool will = NightMare::MqttMayHoldPresence();
    requestedWill = will;
    return NmMqttEsp::state() == -1 ? NmMqttEsp::begin(localBroker(profile), will)
                                    : NmMqttEsp::changeTo(localBroker(profile), will);
}

void messageReceived(const std::string &topic, const std::string &payload, bool retained)
{
    NightMare::OnMessageIngress(NightMare::ConnectionType::MQTT, topic, payload, retained);
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
    // Routing may have moved while this session was being set up.
    NmMqttConnection::onRoutingChanged();
}

void disconnected(bool isLocalBroker)
{
    (void)isLocalBroker;
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
    (void)isLocalBroker;
    if (connectionErrors.fetch_add(1) + 1 < ErrorsBeforeSwitchFailure)
        return;
    connectionErrors.store(0);
    serviceState = NightMare::ConnectivityState::ERROR;
    NightMare::OnConnectionFailedIngress(NightMare::ConnectionType::MQTT);
    serviceChanged();
}

bool applyEnable(NightMare::MqttProfile mqttProfile)
{
    using NightMare::ConnectivityState;
    if (!profileSupported(mqttProfile) || !NightMare::WiFiIP_enabled())
        return false;
    const bool profileChanged = serviceProfile != mqttProfile;
    serviceProfile = mqttProfile;
    if (serviceEnabled && !profileChanged)
        return true;
    serviceEnabled = true;
    serviceState = ConnectivityState::STARTING;
    NmMqttEsp::setHandlers(messageReceived, connected, disconnected, connectionError);
    if (NightMare::WiFiIP_state() == ConnectivityState::CONNECTED)
    {
        serviceState = ConnectivityState::CONNECTING;
        if (!startDriver(mqttProfile))
        {
            serviceState = ConnectivityState::ERROR;
            serviceChanged();
            return false;
        }
    }
    serviceChanged();
    return true;
}

bool applyDisable()
{
    using NightMare::ConnectivityState;
    if (!serviceEnabled)
        return true;
    serviceEnabled = false;
    serviceState = ConnectivityState::STOPPED;
    NightMare::OnDisconnectedIngress(NightMare::ConnectionType::MQTT);
    serviceChanged();
    NmMqttEsp::end();
    return true;
}
}

namespace NmMqttConnection
{
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
    if (!startDriver(serviceProfile))
        serviceState = NightMare::ConnectivityState::ERROR;
    serviceChanged();
}

void onRoutingChanged()
{
    if (!serviceEnabled || NmMqttEsp::state() == -1)
        return;
    const bool will = NightMare::MqttMayHoldPresence();
    if (will == requestedWill)
        return;
    LOG("MQTT", "%s presence: reconnecting %s a Last Will", will ? "Taking" : "Handing over",
        will ? "with" : "without");
    if (!startDriver(serviceProfile))
        LOG_WARNING("MQTT", "Could not reconnect to change the Last Will");
}

bool presenceReady()
{
    return serviceEnabled && NmMqttEsp::sessionHoldsPresence();
}

bool queueAsyncMessage(const std::string &topic, const std::string &message,
                       bool insertOwner, bool retained)
{
    const std::string fullTopic = insertOwner ? gDeviceIdentity.topic(topic) : topic;
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
    if (!applyEnable(mqttProfile))
        return false;
    saveSettings(true, mqttProfile);
    return true;
}

bool Mqtt_enable()
{
    return Mqtt_enable(supportedProfile(static_cast<MqttProfile>(mqttProfileSetting.value())));
}

bool Mqtt_disable()
{
    if (!applyDisable())
        return false;
    saveSettings(false, serviceProfile);
    return true;
}

bool Mqtt_enabled() { return serviceEnabled; }
ConnectivityState Mqtt_state() { return serviceState; }
MqttProfile Mqtt_profile() { return serviceProfile; }
bool Mqtt_profileSupported(MqttProfile profile) { return profileSupported(profile); }
bool Mqtt_savedEnabled() { return mqttEnabledSetting.value(); }
}

#endif // NM_ENABLE_MQTT
