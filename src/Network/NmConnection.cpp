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
#if NM_ENABLE_WIFI_RADIO
#include <Network/WiFiRadio/NmWifiRadioService.h>
#endif
#if NM_ENABLE_WIFI
#include <Network/WiFiIP/NmWifiService.h>
#endif
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
        constexpr size_t MaxTopicFilterLength = 192;

        volatile ConnectionType selectedConnection = ConnectionType::AUTO;
        volatile ConnectionState connectionState = ConnectionState::STOPPED;
        bool resourceConnectionAttached = false;
        bool coordinatorBegun = false;
        std::atomic<ConnectHandler> applicationConnectHandler{nullptr};
        std::atomic<NetworkStatusHandler> applicationNetworkStatusHandler{nullptr};
        constexpr size_t NetworkLayerCount = 4;
        // Last state reported per layer. STOPPED is 0, the state every layer starts in.
        std::atomic<uint8_t> lastLayerState[NetworkLayerCount] = {};

        ConnectivityState currentLayerState(NetworkLayer layer)
        {
            switch (layer)
            {
            case NetworkLayer::WIFI_RADIO:
#if NM_ENABLE_WIFI_RADIO
                return WiFiRadio_state();
#else
                break;
#endif
            case NetworkLayer::WIFI_IP:
#if NM_ENABLE_WIFI
                return WiFiIP_state();
#else
                break;
#endif
            case NetworkLayer::MQTT:
#if NM_ENABLE_MQTT
                return Mqtt_state();
#else
                break;
#endif
            case NetworkLayer::ESP_NOW:
#if NM_NETWORK_ESPNOW
                return EspNow_state();
#else
                break;
#endif
            }
            return ConnectivityState::STOPPED;
        }

        // Compares every layer with what was last reported and calls the handler for each
        // that moved. The exchange makes each transition reported exactly once even when
        // two tasks get here at the same time.
        void reportNetworkStatus()
        {
            const NetworkStatusHandler handler = applicationNetworkStatusHandler.load();
            for (size_t i = 0; i < NetworkLayerCount; ++i)
            {
                const NetworkLayer layer = static_cast<NetworkLayer>(i);
                const ConnectivityState state = currentLayerState(layer);
                const ConnectivityState previous = static_cast<ConnectivityState>(
                    lastLayerState[i].exchange(static_cast<uint8_t>(state)));
                if (handler == nullptr || state == previous)
                    continue;
                const NetworkStatusEvent event = {
                    layer, state, previous,
                    state == (layer == NetworkLayer::WIFI_RADIO ? ConnectivityState::READY
                                                                : ConnectivityState::CONNECTED)};
                handler(event);
            }
        }
        std::atomic<MessageHandler> applicationMessageHandler{nullptr};

        // PublishTextWhenConnected() backlog, flushed on every connect whatever the
        // connection. Guarded by deferredMutex.
        constexpr size_t DeferredCapacity = 5;
        struct DeferredMessage
        {
            bool active = false;
            std::string topic;
            std::string payload;
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
            bool publish(const std::string &topic, const std::string &payload, bool retained) override
            {
                return Publish(topic.c_str(),
                               reinterpret_cast<const uint8_t *>(payload.c_str()),
                               payload.length(), retained);
            }

            bool subscribe(const std::string &topicFilter) override
            {
                return Subscribe(topicFilter.c_str());
            }

            bool unsubscribe(const std::string &topicFilter) override
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

        void notifyConnectedOwners()
        {
            gResourcesManager.subscribeAll();
            GatewayCandidateOnConnected();
            NmMessageRouter::onConnected();
            ConnectHandler handler = applicationConnectHandler.load();
            if (handler != nullptr)
                handler();
            flushDeferred();
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
            selectedConnection = connection;
            connectionState = connection == ConnectionType::AUTO
                                  ? ConnectionState::STOPPED
                                  : ConnectionState::CONNECTED;
            if (coordinatorBegun && connection != ConnectionType::AUTO &&
                (connection != previous || previousState != ConnectionState::CONNECTED))
                notifyConnectedOwners();
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
        if (!validTopicFilter(topicFilter) || connectionState != ConnectionState::CONNECTED)
            return false;
        return driverSubscribe(topicFilter);
    }

    bool Unsubscribe(const char *topicFilter)
    {
        if (!validTopicFilter(topicFilter) || connectionState != ConnectionState::CONNECTED)
            return false;
        return driverUnsubscribe(topicFilter);
    }

    void OnConnect(ConnectHandler handler)
    {
        applicationConnectHandler.store(handler);
    }

    void OnMessage(MessageHandler handler)
    {
        applicationMessageHandler.store(handler);
    }

    void OnNetworkStatus(NetworkStatusHandler handler)
    {
        // Layers already running report their next change, not the state they are in.
        for (size_t i = 0; i < NetworkLayerCount; ++i)
            lastLayerState[i].store(static_cast<uint8_t>(currentLayerState(static_cast<NetworkLayer>(i))));
        applicationNetworkStatusHandler.store(handler);
    }

    const char *NetworkLayerName(NetworkLayer layer)
    {
        switch (layer)
        {
        case NetworkLayer::WIFI_RADIO: return "WIFI_RADIO";
        case NetworkLayer::WIFI_IP: return "WIFI_IP";
        case NetworkLayer::MQTT: return "MQTT";
        case NetworkLayer::ESP_NOW: return "ESP_NOW";
        }
        return "UNKNOWN";
    }

    bool SelectConnection(ConnectionType connection)
    {
        if (connection != ConnectionType::AUTO && !connectionEnabled(connection))
            return false;

        std::string request = "set ";
        request += preferredConnection.name();
        request += ' ';
        request += std::to_string(static_cast<int>(connection));
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
        const ConnectionType before = static_cast<ConnectionType>(selectedConnection);
        const ConnectionState beforeState = static_cast<ConnectionState>(connectionState);
        coordinatorBegun = true;
        reevaluateRouting();
        if (before == selectedConnection && beforeState == ConnectionState::CONNECTED &&
            connectionState == ConnectionState::CONNECTED)
            notifyConnectedOwners();
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
        reportNetworkStatus();
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

    bool PublishText(const std::string &topic, const std::string &payload, bool retained)
    {
        return Publish(topic.c_str(),
                       reinterpret_cast<const uint8_t *>(payload.c_str()),
                       payload.length(), retained);
    }

    bool PublishTextWhenConnected(const std::string &topic, const std::string &payload, bool retained)
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

    std::string ConnectionDeviceStatusJson(const std::string &deviceName, bool online)
    {
        JsonDocument doc;
        doc["name"] = deviceName;
        doc["hardware"] = gDeviceIdentity.getHardwareSignature();
        doc["timezone"] = gDeviceIdentity.getTimezone();
        doc["online"] = online;
        std::string payload;
        serializeJson(doc, payload);
        return payload;
    }

    std::string ConnectionDeviceStatusJson(bool online)
    {
        return ConnectionDeviceStatusJson(gDeviceIdentity.getDeviceName(), online);
    }
}

#endif // NM_ENABLE_NETWORK
