#include <NightMare/Features.h>
#if NM_ENABLE_MQTT
#include "MQTT.h"
#include "NmMqttEsp.h"
#include "NmMessageRouter.h"

#include <Core/DeviceIdentity.h>
#include <Core/ResourcesManager.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace
{
    constexpr int QueueCapacity = 5;
    constexpr int MaxCustomSubscriptions = 16;
    constexpr size_t MaxTopicFilterLength = 192;
    struct QueuedMessage
    {
        bool active = false;
        String topic;
        String payload;
        bool retained = false;
    };

    QueuedMessage pending[QueueCapacity];
    SemaphoreHandle_t queueMutex = nullptr;
    SemaphoreHandle_t subscriptionMutex = nullptr;
    String customSubscriptions[MaxCustomSubscriptions];
    bool discoveryEnabled = false;
    bool discoveryDirty = false;
    bool isCustomSubscription(const String &topicFilter);

    bool isDefaultSubscription(const String &topicFilter)
    {
#if NM_ENABLE_CONSOLE
        if (topicFilter == gDeviceIdentity.topic("console/in") ||
            topicFilter == gDeviceIdentity.topic("console/controlled/+/in") ||
            topicFilter == "all/console/in")
            return true;
#endif
#if NM_ENABLE_TIME_SYNC
        if (topicFilter == "Control/time")
            return true;
#endif
        return false;
    }
    void (*projectMessage)(String, String) = nullptr;
    void (*projectConnected)() = nullptr;
    void (*projectDisconnected)(bool) = nullptr;
    bool deviceMessagesOnly = true;

    class MqttResourceTransport : public ResourcePublisher, public ResourceSubscriber
    {
    public:
        bool publish(const String &topic, const String &payload, bool retained) override
        {
            return MQTT_Publish(topic, payload, false, retained);
        }
        bool subscribe(const String &topicFilter) override
        {
            return NmMqttEsp::subscribe(topicFilter);
        }
        bool unsubscribe(const String &topicFilter) override
        {
            return isCustomSubscription(topicFilter) || NmMqttEsp::unsubscribe(topicFilter);
        }
    };
    MqttResourceTransport resourceTransport;

    bool ensureSubscriptionMutex()
    {
        if (subscriptionMutex == nullptr)
            subscriptionMutex = xSemaphoreCreateMutex();
        return subscriptionMutex != nullptr;
    }

    bool isCustomSubscription(const String &topicFilter)
    {
        if (!ensureSubscriptionMutex())
            return false;
        xSemaphoreTake(subscriptionMutex, portMAX_DELAY);
        bool found = false;
        for (const String &filter : customSubscriptions)
            if (filter == topicFilter)
            {
                found = true;
                break;
            }
        xSemaphoreGive(subscriptionMutex);
        return found;
    }

    bool validTopicFilter(const String &filter)
    {
        if (filter.length() == 0 || filter.length() > MaxTopicFilterLength)
            return false;
        for (size_t i = 0; i < filter.length(); ++i)
        {
            const char c = filter[i];
            if (static_cast<uint8_t>(c) < 0x20)
                return false;
            if (c == '+' && (i != 0 && filter[i - 1] != '/'))
                return false;
            if (c == '+' && (i + 1 != filter.length() && filter[i + 1] != '/'))
                return false;
            if (c == '#' && ((i != 0 && filter[i - 1] != '/') || i + 1 != filter.length()))
                return false;
        }
        return true;
    }

    void subscribeDefaults()
    {
#if NM_ENABLE_CONSOLE
        NmMqttEsp::subscribe(gDeviceIdentity.topic("console/in"));
        NmMqttEsp::subscribe(gDeviceIdentity.topic("console/controlled/+/in"));
        NmMqttEsp::subscribe("all/console/in");
#endif
#if NM_ENABLE_TIME_SYNC
        NmMqttEsp::subscribe("Control/time");
#endif
        gResourcesManager.subscribeAll();

        String filters[MaxCustomSubscriptions];
        bool discover = false;
        if (ensureSubscriptionMutex())
        {
            xSemaphoreTake(subscriptionMutex, portMAX_DELAY);
            discover = discoveryEnabled;
            for (int i = 0; i < MaxCustomSubscriptions; ++i)
                filters[i] = customSubscriptions[i];
            xSemaphoreGive(subscriptionMutex);
        }
        if (discover)
        {
            NmMqttEsp::subscribe("+/resources");
            NmMqttEsp::subscribe("+/status");
        }
        for (const String &filter : filters)
            if (filter.length() != 0 && !isDefaultSubscription(filter) &&
                !gResourcesManager.needsSubscription(filter) &&
                !(discover && (filter == "+/resources" || filter == "+/status")))
                NmMqttEsp::subscribe(filter);
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
        subscribeDefaults();
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
    gResourcesManager.setSubscriber(&resourceTransport);
    gResourcesManager.setPublisher(&resourceTransport);
    NmMqttEsp::setHandlers(messageReceived, connected, disconnected);
    NmMqttEsp::begin(localBroker);
}

void MQTT_End() { NmMqttEsp::end(); }
void MQTT_Finish()
{
    NmMqttEsp::finish();
    gResourcesManager.setSubscriber(nullptr);
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

bool MQTT_SubscribeTopic(const String &topicFilter)
{
    LOG("MQTT", "Subscribing to topic filter: %s", topicFilter.c_str());
    if (!validTopicFilter(topicFilter) || !ensureSubscriptionMutex())
    {
        LOG_ERROR("MQTT", "Invalid topic filter: %s", topicFilter.c_str());
        return false;
    }
    LOG("MQTT", "Topic filter validated and subscription mutex ready: %s", topicFilter.c_str());
    xSemaphoreTake(subscriptionMutex, portMAX_DELAY);
    LOG("MQTT", "Checking subscription slots for: %s", topicFilter.c_str());
    int empty = -1;
    for (int i = 0; i < MaxCustomSubscriptions; ++i)
    {
        if (customSubscriptions[i] == topicFilter)
        {
            LOG("MQTT", "Topic filter already exists in slot %d: %s", i, topicFilter.c_str());
            xSemaphoreGive(subscriptionMutex);
            if (!NmMqttEsp::connected() || isDefaultSubscription(topicFilter) ||
                gResourcesManager.needsSubscription(topicFilter) ||
                (MQTT_DiscoveryEnabled() &&
                 (topicFilter == "+/resources" || topicFilter == "+/status")))
            {
                LOG("MQTT", "Existing subscription requires no broker action: %s", topicFilter.c_str());
                return true;
            }
            LOG("MQTT", "Re-subscribing existing topic filter: %s", topicFilter.c_str());
            return NmMqttEsp::subscribe(topicFilter);
        }
        if (empty < 0 && customSubscriptions[i].length() == 0)
        {
            empty = i;
            LOG("MQTT", "Found available subscription slot: %d", empty);
        }
    }
    if (empty < 0)
    {
        LOG_ERROR("MQTT", "No space for new subscription: %s", topicFilter.c_str());
        xSemaphoreGive(subscriptionMutex);
        return false;
    }
    customSubscriptions[empty] = topicFilter;
    LOG("MQTT", "Stored topic filter in slot %d: %s", empty, topicFilter.c_str());
    xSemaphoreGive(subscriptionMutex);
    if (!NmMqttEsp::connected() || isDefaultSubscription(topicFilter) ||
        gResourcesManager.needsSubscription(topicFilter) ||
        (MQTT_DiscoveryEnabled() &&
         (topicFilter == "+/resources" || topicFilter == "+/status")))
    {
        LOG("MQTT", "Subscription accepted without broker action: %s", topicFilter.c_str());
        return true;
    }
    if (NmMqttEsp::subscribe(topicFilter))
    {
        LOG("MQTT", "Broker subscription succeeded: %s", topicFilter.c_str());
        return true;
    }
    LOG_ERROR("MQTT", "Broker subscription failed: %s", topicFilter.c_str());
    xSemaphoreTake(subscriptionMutex, portMAX_DELAY);
    if (customSubscriptions[empty] == topicFilter)
    {
        customSubscriptions[empty] = String();
        LOG("MQTT", "Removed failed subscription from slot %d: %s", empty, topicFilter.c_str());
    }
    xSemaphoreGive(subscriptionMutex);
    LOG_ERROR("MQTT", "Subscription failed: %s", topicFilter.c_str());
    return false;
}

bool MQTT_UnsubscribeTopic(const String &topicFilter)
{
    if (!ensureSubscriptionMutex())
        return false;
    xSemaphoreTake(subscriptionMutex, portMAX_DELAY);
    int found = -1;
    for (int i = 0; i < MaxCustomSubscriptions; ++i)
        if (customSubscriptions[i] == topicFilter)
        {
            found = i;
            break;
        }
    xSemaphoreGive(subscriptionMutex);
    if (found < 0)
        return false;
    if (NmMqttEsp::connected() && !isDefaultSubscription(topicFilter) &&
        !gResourcesManager.needsSubscription(topicFilter) &&
        !(MQTT_DiscoveryEnabled() &&
          (topicFilter == "+/resources" || topicFilter == "+/status")) &&
        !NmMqttEsp::unsubscribe(topicFilter))
        return false;
    xSemaphoreTake(subscriptionMutex, portMAX_DELAY);
    if (customSubscriptions[found] == topicFilter)
        customSubscriptions[found] = String();
    xSemaphoreGive(subscriptionMutex);
    return true;
}

bool MQTT_SetDiscovery(bool enabled)
{
    if (!ensureSubscriptionMutex())
        return false;
    xSemaphoreTake(subscriptionMutex, portMAX_DELAY);
    const bool changed = discoveryEnabled != enabled;
    const bool retry = discoveryDirty;
    discoveryEnabled = enabled;
    discoveryDirty = false;
    xSemaphoreGive(subscriptionMutex);
    if (!NmMqttEsp::connected() || (!changed && !retry))
        return true;
    const bool manifests = isCustomSubscription("+/resources") ||
                           (enabled ? NmMqttEsp::subscribe("+/resources")
                                    : NmMqttEsp::unsubscribe("+/resources"));
    const bool statuses = isCustomSubscription("+/status") ||
                          (enabled ? NmMqttEsp::subscribe("+/status")
                                   : NmMqttEsp::unsubscribe("+/status"));
    const bool accepted = manifests && statuses;
    if (!accepted)
    {
        xSemaphoreTake(subscriptionMutex, portMAX_DELAY);
        discoveryDirty = true;
        xSemaphoreGive(subscriptionMutex);
    }
    return accepted;
}

bool MQTT_DiscoveryEnabled()
{
    if (!ensureSubscriptionMutex())
        return false;
    xSemaphoreTake(subscriptionMutex, portMAX_DELAY);
    const bool enabled = discoveryEnabled;
    xSemaphoreGive(subscriptionMutex);
    return enabled;
}

String deviceStatusJson(const String &deviceName, bool online)
{
    // Serialized, not concatenated: a device name may legally contain '"' or '\'.
    StaticJsonDocument<256> doc;
    doc["name"] = deviceName;
    doc["hardware"] = gDeviceIdentity.getHardwareSignature();
    doc["online"] = online;
    String payload;
    serializeJson(doc, payload);
    return payload;
}

String deviceStatusJson(bool online)
{
    return deviceStatusJson(gDeviceIdentity.getDeviceName(), online);
}

void MQTT_onMessage(void (*cb)(String, String), bool onlyDeviceMessages)
{
    projectMessage = cb;
    deviceMessagesOnly = onlyDeviceMessages;
}
void MQTT_onConnected(void (*cb)()) { projectConnected = cb; }
void MQTT_onDisconnected(void (*cb)(bool)) { projectDisconnected = cb; }
#endif // NM_ENABLE_MQTT
