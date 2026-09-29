#include <NightMare/Features.h>
#if NM_ENABLE_NETWORK

#include "NmTransport.h"
#include "NmTransportInternal.h"

#include <Core/ResourcesManager.h>
#include <Core/DeviceIdentity.h>
#if NM_ENABLE_MQTT
#include <Network/MQTT/MQTT.h>
#include <Network/MQTT/NmMqttEsp.h>
#endif

namespace NightMare
{
Config<int> preferredTransport("nightmare:connection:preferred_transport",
                               static_cast<int>(TransportType::MQTT));

namespace
{
constexpr size_t MaxSubscriptions = 16;

volatile TransportType selectedTransport = TransportType::AUTO;
volatile TransportState transportState = TransportState::STOPPED;
String subscriptions[MaxSubscriptions];

bool isMqtt(TransportType transport)
{
    return transport == TransportType::MQTT ||
           transport == TransportType::LOCAL_MQTT;
}

bool validTopicFilter(const char *topicFilter)
{
    return topicFilter != nullptr && topicFilter[0] != '\0';
}

class TransportResourceAdapter : public ResourcePublisher, public ResourceSubscriber
{
public:
    bool publish(const String &topic, const String &payload, bool retained) override
    {
        return Publish(topic.c_str(),
                       reinterpret_cast<const uint8_t *>(payload.c_str()),
                       payload.length(), retained);
    }

    bool subscribe(const String &topicFilter) override
    {
#if NM_ENABLE_MQTT
        return isMqtt(static_cast<TransportType>(selectedTransport)) &&
               NmMqttEsp::subscribe(topicFilter);
#else
        (void)topicFilter;
        return false;
#endif
    }

    bool unsubscribe(const String &topicFilter) override
    {
#if NM_ENABLE_MQTT
        return isMqtt(static_cast<TransportType>(selectedTransport)) &&
               NmMqttEsp::unsubscribe(topicFilter);
#else
        (void)topicFilter;
        return false;
#endif
    }
};

TransportResourceAdapter resourceAdapter;

void attachResourceTransport()
{
    gResourcesManager.setPublisher(&resourceAdapter);
    gResourcesManager.setSubscriber(&resourceAdapter);
}

void restoreSubscriptions()
{
#if NM_ENABLE_MQTT
    if (!isMqtt(static_cast<TransportType>(selectedTransport)))
        return;
    for (const String &filter : subscriptions)
        if (filter.length() != 0)
            NmMqttEsp::subscribe(filter);
#endif
}

bool startTransport(TransportType transport)
{
    if (!isMqtt(transport))
        return false;
#if NM_ENABLE_MQTT
    if (transportState == TransportState::CONNECTED &&
        selectedTransport == transport)
        return true;

    attachResourceTransport();
    selectedTransport = transport;
    transportState = TransportState::CONNECTING;
    const bool local = transport == TransportType::LOCAL_MQTT;
    if (MQTT_State() == -1)
        MQTT_Init(local);
    else
        MQTT_change_to(local);
    return true;
#else
    (void)transport;
    transportState = TransportState::ERROR;
    return false;
#endif
}

bool changePreferredTransport(Config<int> &, const int &requested)
{
    return startTransport(static_cast<TransportType>(requested));
}

struct PreferredTransportHandlerInstaller
{
    PreferredTransportHandlerInstaller()
    {
        preferredTransport.onWrite = changePreferredTransport;
    }
};

PreferredTransportHandlerInstaller preferredTransportHandlerInstaller;
}

bool Publish(const char *topic, const uint8_t *payload, size_t length, bool retained)
{
    if (topic == nullptr || topic[0] == '\0' ||
        (payload == nullptr && length != 0) ||
        transportState != TransportState::CONNECTED)
        return false;

    switch (selectedTransport)
    {
    case TransportType::MQTT:
    case TransportType::LOCAL_MQTT:
#if NM_ENABLE_MQTT
    {
        String message;
        if (length != 0 &&
            (!message.reserve(length) ||
             !message.concat(reinterpret_cast<const char *>(payload), length)))
            return false;
        return NmMqttEsp::publish(String(topic), message, retained);
    }
#else
        return false;
#endif
    case TransportType::AUTO:
    case TransportType::ESP_NOW:
        return false;
    }
    return false;
}

bool Subscribe(const char *topicFilter)
{
    if (!validTopicFilter(topicFilter))
        return false;
    const String filter(topicFilter);
    int empty = -1;
    for (size_t i = 0; i < MaxSubscriptions; ++i)
    {
        if (subscriptions[i] == filter)
            return true;
        if (empty < 0 && subscriptions[i].length() == 0)
            empty = static_cast<int>(i);
    }
    if (empty < 0)
        return false;

    if (transportState == TransportState::CONNECTED)
    {
#if NM_ENABLE_MQTT
        if (!isMqtt(selectedTransport) || !NmMqttEsp::subscribe(filter))
            return false;
#else
        return false;
#endif
    }
    subscriptions[empty] = filter;
    return true;
}

bool Unsubscribe(const char *topicFilter)
{
    if (!validTopicFilter(topicFilter))
        return false;
    const String filter(topicFilter);
    int found = -1;
    for (size_t i = 0; i < MaxSubscriptions; ++i)
        if (subscriptions[i] == filter)
        {
            found = static_cast<int>(i);
            break;
        }
    if (found < 0)
        return false;

    if (transportState == TransportState::CONNECTED)
    {
#if NM_ENABLE_MQTT
        if (!isMqtt(selectedTransport) || !NmMqttEsp::unsubscribe(filter))
            return false;
#else
        return false;
#endif
    }
    subscriptions[found] = String();
    return true;
}

bool SelectTransport(TransportType transport)
{
    if (!isMqtt(transport))
        return false;
    if (!preferredTransport.set(static_cast<int>(transport)))
    {
        transportState = TransportState::ERROR;
        return false;
    }
    return startTransport(transport);
}

TransportType GetSelectedTransport()
{
    return static_cast<TransportType>(selectedTransport);
}

TransportState GetTransportState()
{
#if NM_ENABLE_MQTT
    if (isMqtt(static_cast<TransportType>(selectedTransport)))
    {
        const int8_t mqttState = MQTT_State();
        if (mqttState > 0)
            return TransportState::CONNECTED;
        if (mqttState == -1 && transportState != TransportState::CONNECTING)
            return TransportState::STOPPED;
        if (mqttState == -2 || mqttState == 0)
            return TransportState::CONNECTING;
    }
#endif
    return transportState;
}

void TransportConnectedIngress(bool localMqtt)
{
    selectedTransport = localMqtt ? TransportType::LOCAL_MQTT
                                  : TransportType::MQTT;
    transportState = TransportState::CONNECTED;
    if (preferredTransport.value() != static_cast<int>(selectedTransport))
        preferredTransport.set(static_cast<int>(selectedTransport));
    restoreSubscriptions();
}

void TransportMqttStarting(bool localMqtt)
{
    selectedTransport = localMqtt ? TransportType::LOCAL_MQTT
                                  : TransportType::MQTT;
    transportState = TransportState::CONNECTING;
    attachResourceTransport();
    if (preferredTransport.value() != static_cast<int>(selectedTransport))
        preferredTransport.set(static_cast<int>(selectedTransport));
}

void TransportDisconnectedIngress()
{
    if (isMqtt(static_cast<TransportType>(selectedTransport)) &&
        transportState != TransportState::STOPPED)
        transportState = TransportState::CONNECTING;
}

void TransportMqttStopping(bool finish)
{
    transportState = TransportState::STOPPED;
    if (finish)
    {
        gResourcesManager.setPublisher(nullptr);
        gResourcesManager.setSubscriber(nullptr);
    }
}

bool PublishText(const String &topic, const String &payload, bool retained)
{
    return Publish(topic.c_str(),
                   reinterpret_cast<const uint8_t *>(payload.c_str()),
                   payload.length(), retained);
}

bool PublishDeviceText(const String &topic, const String &payload, bool retained)
{
    return PublishText(gDeviceIdentity.topic(topic), payload, retained);
}

String TransportDeviceStatusJson(bool online)
{
#if NM_ENABLE_MQTT
    return deviceStatusJson(online);
#else
    (void)online;
    return String();
#endif
}

String TransportDeviceStatusJson(const String &deviceName, bool online)
{
#if NM_ENABLE_MQTT
    return deviceStatusJson(deviceName, online);
#else
    (void)deviceName;
    (void)online;
    return String();
#endif
}
}

#endif // NM_ENABLE_NETWORK
