#include <NightMare/Features.h>
#if NM_ENABLE_NETWORK

#include "NmTransport.h"
#include "NmTransportInternal.h"

#include <Core/ConfigManager.h>
#include <Core/DeviceIdentity.h>
#include <Core/Logs.h>
#include <Core/ResourcesManager.h>
#include <Network/NmMessageRouter.h>
#if NM_ENABLE_MQTT
#include <Network/MQTT/NmMqttTransport.h>
#endif
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace NightMare
{
Config<int> preferredTransport("nightmare:connection:preferred_transport",
                               static_cast<int>(TransportType::MQTT));

namespace
{
// Two filters per maximum Remote Resource, plus framework and application
// headroom. One registry replaces both the old MQTT custom list and direct RM
// reconnect subscriptions.
constexpr size_t MaxSubscriptions = 256;
constexpr size_t MaxTopicFilterLength = 192;

volatile TransportType selectedTransport = TransportType::AUTO;
volatile TransportState transportState = TransportState::STOPPED;
struct Subscription
{
    String filter;
    uint16_t references = 0;
};
Subscription subscriptions[MaxSubscriptions];
SemaphoreHandle_t subscriptionMutex = nullptr;
bool frameworkSubscriptionsRegistered = false;
bool resourceTransportAttached = false;
TransportType rollbackTransport = TransportType::AUTO;
bool rollbackAvailable = false;

bool isMqtt(TransportType transport)
{
    return transport == TransportType::MQTT ||
           transport == TransportType::LOCAL_MQTT;
}

bool validTopicFilter(const char *topicFilter)
{
    if (topicFilter == nullptr)
        return false;
    const size_t length = strlen(topicFilter);
    if (length == 0 || length > MaxTopicFilterLength)
        return false;

    for (size_t i = 0; i < length; ++i)
    {
        const char c = topicFilter[i];
        if (static_cast<uint8_t>(c) < 0x20)
            return false;
        if (c == '+' && (i != 0 && topicFilter[i - 1] != '/'))
            return false;
        if (c == '+' && (i + 1 != length && topicFilter[i + 1] != '/'))
            return false;
        if (c == '#' && ((i != 0 && topicFilter[i - 1] != '/') || i + 1 != length))
            return false;
    }
    return true;
}

bool ensureSubscriptionMutex()
{
    if (subscriptionMutex == nullptr)
        subscriptionMutex = xSemaphoreCreateMutex();
    return subscriptionMutex != nullptr;
}

bool driverSubscribe(const char *topicFilter)
{
#if NM_ENABLE_MQTT
    return isMqtt(static_cast<TransportType>(selectedTransport)) &&
           NmMqttTransport::subscribe(topicFilter);
#else
    (void)topicFilter;
    return false;
#endif
}

bool driverUnsubscribe(const char *topicFilter)
{
#if NM_ENABLE_MQTT
    return isMqtt(static_cast<TransportType>(selectedTransport)) &&
           NmMqttTransport::unsubscribe(topicFilter);
#else
    (void)topicFilter;
    return false;
#endif
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
        return Subscribe(topicFilter.c_str());
    }

    bool unsubscribe(const String &topicFilter) override
    {
        return Unsubscribe(topicFilter.c_str());
    }
};

TransportResourceAdapter resourceAdapter;

void attachResourceTransport()
{
    if (resourceTransportAttached)
        return;
    resourceTransportAttached = true;
    gResourcesManager.setSubscriber(&resourceAdapter);
    gResourcesManager.setPublisher(&resourceAdapter);
}

bool registerFrameworkSubscriptions()
{
    if (frameworkSubscriptionsRegistered)
        return true;

    bool complete = true;
#if NM_ENABLE_CONSOLE
    complete = Subscribe(gDeviceIdentity.topic("console/in").c_str()) && complete;
    complete = Subscribe(gDeviceIdentity.topic("console/controlled/+/in").c_str()) && complete;
    complete = Subscribe("all/console/in") && complete;
#endif
#if NM_ENABLE_TIME_SYNC
    complete = Subscribe("Control/time") && complete;
#endif
    gResourcesManager.subscribeAll();
    frameworkSubscriptionsRegistered = true;
    if (!complete)
        LOG_WARNING("NET", "One or more framework subscriptions could not be registered");
    return complete;
}

void restoreSubscriptions()
{
    if (!ensureSubscriptionMutex())
        return;

    xSemaphoreTake(subscriptionMutex, portMAX_DELAY);
    for (const Subscription &subscription : subscriptions)
    {
        if (subscription.references != 0 &&
            !driverSubscribe(subscription.filter.c_str()))
            LOG_WARNING("NET", "Could not restore subscription: %s",
                        subscription.filter.c_str());
    }
    xSemaphoreGive(subscriptionMutex);
}

bool startTransport(TransportType transport)
{
    if (!isMqtt(transport))
        return false;
#if NM_ENABLE_MQTT
    if (selectedTransport == transport &&
        (transportState == TransportState::CONNECTING ||
         transportState == TransportState::CONNECTED))
        return true;

    attachResourceTransport();
    registerFrameworkSubscriptions();

    const TransportType previousTransport = selectedTransport;
    const TransportState previousState = transportState;
    const TransportType previousRollbackTransport = rollbackTransport;
    const bool previousRollbackAvailable = rollbackAvailable;
    rollbackAvailable = previousState == TransportState::CONNECTED &&
                        isMqtt(previousTransport) && previousTransport != transport;
    if (rollbackAvailable)
        rollbackTransport = previousTransport;
    selectedTransport = transport;
    transportState = TransportState::CONNECTING;

    const bool accepted = NmMqttTransport::state() == -1
                              ? NmMqttTransport::begin(transport)
                              : NmMqttTransport::changeTo(transport);
    if (!accepted)
    {
        selectedTransport = previousTransport;
        transportState = previousState;
        rollbackTransport = previousRollbackTransport;
        rollbackAvailable = previousRollbackAvailable;
    }
    return accepted;
#else
    (void)transport;
    return false;
#endif
}

bool changePreferredTransport(Config<int> &, const int &requested)
{
    const TransportType transport = static_cast<TransportType>(requested);
    return isMqtt(transport) && startTransport(transport);
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
        return NmMqttTransport::publish(topic, payload, length, retained);
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
    if (!validTopicFilter(topicFilter) || !ensureSubscriptionMutex())
        return false;

    const String filter(topicFilter);
    xSemaphoreTake(subscriptionMutex, portMAX_DELAY);
    int empty = -1;
    for (size_t i = 0; i < MaxSubscriptions; ++i)
    {
        if (subscriptions[i].references != 0 && subscriptions[i].filter == filter)
        {
            if (subscriptions[i].references == UINT16_MAX)
            {
                xSemaphoreGive(subscriptionMutex);
                return false;
            }
            ++subscriptions[i].references;
            xSemaphoreGive(subscriptionMutex);
            return true;
        }
        if (empty < 0 && subscriptions[i].references == 0)
            empty = static_cast<int>(i);
    }
    if (empty < 0)
    {
        xSemaphoreGive(subscriptionMutex);
        return false;
    }

    if (transportState == TransportState::CONNECTED && !driverSubscribe(topicFilter))
    {
        xSemaphoreGive(subscriptionMutex);
        return false;
    }
    subscriptions[empty].filter = filter;
    subscriptions[empty].references = 1;
    xSemaphoreGive(subscriptionMutex);
    return true;
}

bool Unsubscribe(const char *topicFilter)
{
    if (!validTopicFilter(topicFilter) || !ensureSubscriptionMutex())
        return false;

    const String filter(topicFilter);
    xSemaphoreTake(subscriptionMutex, portMAX_DELAY);
    int found = -1;
    for (size_t i = 0; i < MaxSubscriptions; ++i)
    {
        if (subscriptions[i].references != 0 && subscriptions[i].filter == filter)
        {
            found = static_cast<int>(i);
            break;
        }
    }
    if (found < 0)
    {
        xSemaphoreGive(subscriptionMutex);
        return false;
    }
    if (subscriptions[found].references > 1)
    {
        --subscriptions[found].references;
        xSemaphoreGive(subscriptionMutex);
        return true;
    }
    if (transportState == TransportState::CONNECTED && !driverUnsubscribe(topicFilter))
    {
        xSemaphoreGive(subscriptionMutex);
        return false;
    }
    subscriptions[found] = Subscription();
    xSemaphoreGive(subscriptionMutex);
    return true;
}

bool SelectTransport(TransportType transport)
{
    if (!isMqtt(transport))
        return false;

    String request = "set ";
    request += preferredTransport.name();
    request += ' ';
    request += String(static_cast<int>(transport));
    return configManager().handle(request) == "OK";
}

TransportType GetSelectedTransport()
{
    return static_cast<TransportType>(selectedTransport);
}

TransportState GetTransportState()
{
    return static_cast<TransportState>(transportState);
}

void TransportConnectedIngress(TransportType transport)
{
    if (transport != selectedTransport)
        return;
    transportState = TransportState::CONNECTED;
    rollbackAvailable = false;
    restoreSubscriptions();
    NmMessageRouter::onConnected();
}

void TransportDisconnectedIngress(TransportType transport)
{
    if (transport == selectedTransport && transportState != TransportState::STOPPED)
        transportState = TransportState::CONNECTING;
}

void TransportConnectionFailedIngress(TransportType transport)
{
    if (transport != selectedTransport)
        return;

    if (!rollbackAvailable)
    {
        transportState = TransportState::ERROR;
        return;
    }

    const TransportType fallback = rollbackTransport;
    rollbackAvailable = false;
    LOG_WARNING("NET", "Connection type %u failed; rolling back to %u",
                static_cast<unsigned>(transport), static_cast<unsigned>(fallback));

    if (!preferredTransport.set(static_cast<int>(fallback)))
        LOG_ERROR("NET", "Could not persist the rollback transport");
    if (!startTransport(fallback))
        transportState = TransportState::ERROR;
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

String TransportDeviceStatusJson(const String &deviceName, bool online)
{
    JsonDocument doc;
    doc["name"] = deviceName;
    doc["hardware"] = gDeviceIdentity.getHardwareSignature();
    doc["timezone"] = gDeviceIdentity.getTimezone();
    doc["online"] = online;
    String payload;
    serializeJson(doc, payload);
    return payload;
}

String TransportDeviceStatusJson(bool online)
{
    return TransportDeviceStatusJson(gDeviceIdentity.getDeviceName(), online);
}
}

#endif // NM_ENABLE_NETWORK
