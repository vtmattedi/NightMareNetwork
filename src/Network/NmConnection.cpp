#include <NightMare/Features.h>
#if NM_ENABLE_NETWORK

#include "NmConnection.h"
#include "NmConnectionInternal.h"
#include "NmRoutingPolicy.h"

#include <Core/ConfigManager.h>
#include <Core/DeviceIdentity.h>
#include <Core/Logs.h>
#include <Core/ResourcesManager.h>
#include <Core/SystemState.h>
#include <Network/NmMessageRouter.h>
#include <Network/Connectivity.h>
#include <Network/GatewayCandidate.h>
#if NM_ENABLE_MQTT
#include <Network/MQTT/NmMqttConnection.h>
#endif
#if NM_NETWORK_ESPNOW
#include <Network/EspNow/NmEspNowConnection.h>
#include <Network/EspNow/EspNowClient.h>
#endif
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <atomic>

namespace NightMare
{
    namespace
    {
        constexpr ConnectionType defaultConnection()
        {
#if NM_NETWORK_MQTT
            return ConnectionType::MQTT;
#elif NM_NETWORK_LOCALMQTT
            return ConnectionType::MQTT;
#elif NM_NETWORK_ESPNOW
            return ConnectionType::ESP_NOW;
#else
            return ConnectionType::AUTO;
#endif
        }
    }
    //@NightMare:Config Preferred Connection
    // Routing intent only. It never enables or disables a connectivity service.
    // AUTO uses the base order: ESP-NOW, MQTT.
    Config<int> preferredConnection("nightmare:connection:preferred_connection",
                                    static_cast<int>(defaultConnection()));

    namespace
    {
        // Two filters per maximum Remote Resource, plus framework and application
        // headroom. One registry replaces both the old MQTT custom list and direct RM
        // reconnect subscriptions.
        constexpr size_t MaxSubscriptions = 256;
        constexpr size_t MaxTopicFilterLength = 192;

        volatile ConnectionType selectedConnection = ConnectionType::AUTO;
        volatile ConnectionState connectionState = ConnectionState::STOPPED;
        struct Subscription
        {
            String filter;
            uint16_t references = 0;
        };
        Subscription subscriptions[MaxSubscriptions];
        SemaphoreHandle_t subscriptionMutex = nullptr;
        bool frameworkSubscriptionsRegistered = false;
        bool resourceConnectionAttached = false;
        bool coordinatorBegun = false;
        std::atomic<MessageHandler> applicationMessageHandler{nullptr};

        // PublishTextWhenConnected() backlog, flushed on every connect whatever the
        // connection. Guarded by deferredMutex.
        constexpr size_t DeferredCapacity = 5;
        struct DeferredMessage
        {
            bool active = false;
            String topic;
            String payload;
            bool retained = false;
        };
        DeferredMessage deferred[DeferredCapacity];
        SemaphoreHandle_t deferredMutex = nullptr;

        bool ensureDeferredMutex()
        {
            if (deferredMutex == nullptr)
                deferredMutex = xSemaphoreCreateMutex();
            return deferredMutex != nullptr;
        }

        void flushDeferred()
        {
            if (deferredMutex == nullptr)
                return;
            for (size_t i = 0; i < DeferredCapacity; ++i)
            {
                xSemaphoreTake(deferredMutex, portMAX_DELAY);
                if (!deferred[i].active)
                {
                    xSemaphoreGive(deferredMutex);
                    continue;
                }
                const DeferredMessage message = deferred[i];
                xSemaphoreGive(deferredMutex);

                if (!PublishText(message.topic, message.payload, message.retained))
                    return; // still not deliverable; the next connect tries again

                xSemaphoreTake(deferredMutex, portMAX_DELAY);
                deferred[i] = DeferredMessage();
                xSemaphoreGive(deferredMutex);
            }
        }

        bool connectionEnabled(ConnectionType connection)
        {
            switch (connection)
            {
            case ConnectionType::MQTT:
                return (NM_NETWORK_MQTT || NM_NETWORK_LOCALMQTT) != 0;
            case ConnectionType::ESP_NOW:
                return NM_NETWORK_ESPNOW != 0;
            case ConnectionType::AUTO:
                return false;
            }
            return false;
        }

        bool transportEnabled(ConnectionType connection)
        {
            switch (connection)
            {
            case ConnectionType::MQTT:
#if NM_ENABLE_MQTT
                return Mqtt_enabled();
#else
                return false;
#endif
            case ConnectionType::ESP_NOW:
#if NM_NETWORK_ESPNOW
                return EspNow_enabled();
#else
                return false;
#endif
            case ConnectionType::AUTO:
                return false;
            }
            return false;
        }

        bool transportUsable(ConnectionType connection)
        {
            if (!transportEnabled(connection))
                return false;
            switch (connection)
            {
            case ConnectionType::MQTT:
#if NM_ENABLE_MQTT
                return Mqtt_state() == ConnectivityState::CONNECTED;
#else
                return false;
#endif
            case ConnectionType::ESP_NOW:
#if NM_NETWORK_ESPNOW
                return EspNow_state() == ConnectivityState::CONNECTED;
#else
                return false;
#endif
            case ConnectionType::AUTO:
                return false;
            }
            return false;
        }

        bool routeEligible(ConnectionType connection)
        {
            if (!transportUsable(connection))
                return false;
            if (connection != ConnectionType::ESP_NOW)
                return true;
#if NM_ENABLE_MQTT && NM_NETWORK_ESPNOW
            // A build with an enabled MQTT service knows which route class it
            // needs. The retained announcement proves readiness; the matching
            // authenticated ESP-NOW session proves the actual peer.
            if (Mqtt_enabled())
                return GatewayCandidateIsProbable(Mqtt_profile()) &&
                       GatewayCandidateMatchesAuthenticated(EspNowClient::gatewayId().c_str());
#endif
            return true;
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
#if NM_NETWORK_ESPNOW
            if (selectedConnection == ConnectionType::ESP_NOW)
                return NmEspNowConnection::subscribe(topicFilter);
#endif
#if NM_ENABLE_MQTT
            return selectedConnection == ConnectionType::MQTT &&
                   NmMqttConnection::subscribe(topicFilter);
#else
            (void)topicFilter;
            return false;
#endif
        }

        bool driverUnsubscribe(const char *topicFilter)
        {
#if NM_NETWORK_ESPNOW
            if (selectedConnection == ConnectionType::ESP_NOW)
                return NmEspNowConnection::unsubscribe(topicFilter);
#endif
#if NM_ENABLE_MQTT
            return selectedConnection == ConnectionType::MQTT &&
                   NmMqttConnection::unsubscribe(topicFilter);
#else
            (void)topicFilter;
            return false;
#endif
        }

        class ConnectionResourceAdapter : public ResourcePublisher, public ResourceSubscriber
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

        ConnectionResourceAdapter resourceAdapter;

        void attachResourceConnection()
        {
            if (resourceConnectionAttached)
                return;
            resourceConnectionAttached = true;
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
            complete = Subscribe(GatewayNetworkTopicFilter()) && complete;
            complete = Subscribe(GatewayStatusTopicFilter()) && complete;
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

        void removeSubscriptionsFromActive()
        {
            if (connectionState != ConnectionState::CONNECTED || subscriptionMutex == nullptr)
                return;
            xSemaphoreTake(subscriptionMutex, portMAX_DELAY);
            for (const Subscription &subscription : subscriptions)
                if (subscription.references != 0)
                    driverUnsubscribe(subscription.filter.c_str());
            xSemaphoreGive(subscriptionMutex);
        }

        void requestNetworkTelemetry()
        {
#if NM_ENABLE_TELEMETRY
            SystemState.request(SystemRequest::PublishTelemetry);
#endif
        }

        void activate(ConnectionType connection)
        {
            const ConnectionType previous = selectedConnection;
            const ConnectionState previousState = connectionState;
            if (connection != previous && previousState == ConnectionState::CONNECTED)
                removeSubscriptionsFromActive();
            selectedConnection = connection;
            connectionState = connection == ConnectionType::AUTO
                                  ? ConnectionState::STOPPED
                                  : ConnectionState::CONNECTED;
            if (coordinatorBegun && connection != ConnectionType::AUTO &&
                (connection != previous || previousState != ConnectionState::CONNECTED))
            {
                restoreSubscriptions();
                NmMessageRouter::onConnected();
                flushDeferred();
            }
            if (connection != previous || connectionState != previousState)
                requestNetworkTelemetry();
        }

        void reevaluateRouting(ConnectionType preference)
        {
            const ConnectionType current = static_cast<ConnectionType>(selectedConnection);
            activate(ResolveNetworkRoute(preference, current,
                                         transportUsable(ConnectionType::MQTT),
                                         transportUsable(ConnectionType::ESP_NOW),
                                         routeEligible(ConnectionType::ESP_NOW)));
        }

        void reevaluateRouting()
        {
            reevaluateRouting(static_cast<ConnectionType>(preferredConnection.value()));
        }

        bool changePreferredConnection(Config<int> &, const int &requested)
        {
            const ConnectionType connection = static_cast<ConnectionType>(requested);
            if (connection != ConnectionType::AUTO && !connectionEnabled(connection))
                return false;
            reevaluateRouting(connection);
            requestNetworkTelemetry();
            return true;
        }

        struct PreferredConnectionHandlerInstaller
        {
            PreferredConnectionHandlerInstaller()
            {
                preferredConnection.onWrite = changePreferredConnection;
            }
        };

        PreferredConnectionHandlerInstaller preferredConnectionHandlerInstaller;
    }

    bool Publish(const char *topic, const uint8_t *payload, size_t length, bool retained)
    {
        if (topic == nullptr || topic[0] == '\0' ||
            (payload == nullptr && length != 0) ||
            connectionState != ConnectionState::CONNECTED)
            return false;

        switch (selectedConnection)
        {
        case ConnectionType::MQTT:
#if NM_ENABLE_MQTT
            return NmMqttConnection::publish(topic, payload, length, retained);
#else
            return false;
#endif
        case ConnectionType::ESP_NOW:
#if NM_NETWORK_ESPNOW
            return NmEspNowConnection::publish(topic, payload, length, retained);
#else
            return false;
#endif
        case ConnectionType::AUTO:
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

        if (connectionState == ConnectionState::CONNECTED && !driverSubscribe(topicFilter))
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
        if (connectionState == ConnectionState::CONNECTED && !driverUnsubscribe(topicFilter))
        {
            xSemaphoreGive(subscriptionMutex);
            return false;
        }
        subscriptions[found] = Subscription();
        xSemaphoreGive(subscriptionMutex);
        return true;
    }

    void OnMessage(MessageHandler handler)
    {
        applicationMessageHandler.store(handler);
    }

    bool SelectConnection(ConnectionType connection)
    {
        if (connection != ConnectionType::AUTO && !connectionEnabled(connection))
            return false;

        String request = "set ";
        request += preferredConnection.name();
        request += ' ';
        request += String(static_cast<int>(connection));
        return configManager().handle(request) == "OK";
    }

    ConnectionType GetActiveConnection()
    {
        return static_cast<ConnectionType>(selectedConnection);
    }
    ConnectionType GetPreferredConnection()
    {
        return static_cast<ConnectionType>(preferredConnection.value());
    }

    ConnectionState GetConnectionState()
    {
        return static_cast<ConnectionState>(connectionState);
    }

    const char *ConnectionTypeName(ConnectionType connection)
    {
        switch (connection)
        {
        case ConnectionType::AUTO: return "AUTO";
        case ConnectionType::MQTT: return "MQTT";
        case ConnectionType::ESP_NOW: return "ESP_NOW";
        }
        return "AUTO";
    }

    const char *MqttProfileName(MqttProfile profile)
    {
        switch (profile)
        {
        case MqttProfile::REMOTE: return "REMOTE";
        case MqttProfile::LOCAL: return "LOCAL";
        }
        return "REMOTE";
    }

    const char *ConnectionStateName(ConnectionState state)
    {
        switch (state)
        {
        case ConnectionState::STOPPED: return "STOPPED";
        case ConnectionState::DISCOVERING: return "DISCOVERING";
        case ConnectionState::CONNECTING: return "CONNECTING";
        case ConnectionState::CONNECTED: return "CONNECTED";
        case ConnectionState::ERROR: return "ERROR";
        }
        return "ERROR";
    }

    void OnConnectedIngress(ConnectionType connection)
    {
        (void)connection;
        reevaluateRouting();
    }

    void OnDisconnectedIngress(ConnectionType connection)
    {
        (void)connection;
        reevaluateRouting();
    }

    void OnConnectionFailedIngress(ConnectionType connection)
    {
        (void)connection;
        reevaluateRouting();
    }

    void ConnectionTick()
    {
        reevaluateRouting();
    }

    bool ConnectionBegin()
    {
        attachResourceConnection();
        registerFrameworkSubscriptions();
        const ConnectionType before = static_cast<ConnectionType>(selectedConnection);
        const ConnectionState beforeState = static_cast<ConnectionState>(connectionState);
        coordinatorBegun = true;
        reevaluateRouting();
        if (before == selectedConnection && beforeState == ConnectionState::CONNECTED &&
            connectionState == ConnectionState::CONNECTED)
        {
            NmMessageRouter::onConnected();
            flushDeferred();
        }
        return true;
    }

    void OnRadioAvailabilityIngress(bool available)
    {
        (void)available;
        OnConnectivityStateChanged();
    }

    void OnIpLinkAvailabilityIngress(bool available)
    {
        (void)available;
        reevaluateRouting();
    }

    void OnConnectivityStateChanged()
    {
        reevaluateRouting();
        requestNetworkTelemetry();
    }

    bool DispatchApplicationMessage(const char *topic, const uint8_t *payload,
                                    size_t length, bool retained)
    {
        MessageHandler handler = applicationMessageHandler.load();
        if (handler == nullptr)
            return false;
        handler(topic, payload, length, retained);
        return true;
    }

    bool PublishText(const String &topic, const String &payload, bool retained)
    {
        return Publish(topic.c_str(),
                       reinterpret_cast<const uint8_t *>(payload.c_str()),
                       payload.length(), retained);
    }

    bool PublishTextWhenConnected(const String &topic, const String &payload, bool retained)
    {
        if (PublishText(topic, payload, retained))
            return true;
        if (!ensureDeferredMutex())
            return false;

        xSemaphoreTake(deferredMutex, portMAX_DELAY);
        for (DeferredMessage &slot : deferred)
        {
            if (slot.active)
                continue;
            slot.topic = topic;
            slot.payload = payload;
            slot.retained = retained;
            slot.active = true;
            xSemaphoreGive(deferredMutex);
            return true;
        }
        xSemaphoreGive(deferredMutex);
        return false;
    }

    String ConnectionDeviceStatusJson(const String &deviceName, bool online)
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

    String ConnectionDeviceStatusJson(bool online)
    {
        return ConnectionDeviceStatusJson(gDeviceIdentity.getDeviceName(), online);
    }
}

#endif // NM_ENABLE_NETWORK
