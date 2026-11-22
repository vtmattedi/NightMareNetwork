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
#include <Core/TopicFilter.h>
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
#if NM_ENABLE_GATEWAY
#include <Gateway/GatewayNode.h>
#endif
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <vector>

namespace NightMare
{
    namespace
    {
        constexpr ConnectionType defaultConnection()
        {
#if NM_ENABLE_GATEWAY
            return ConnectionType::GATEWAY;
#elif NM_ENABLE_MQTT
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
        volatile ConnectionType selectedConnection = ConnectionType::AUTO;
        volatile ConnectionState connectionState = ConnectionState::STOPPED;
        bool resourceConnectionAttached = false;
        bool coordinatorBegun = false;
        std::atomic<ConnectHandler> applicationConnectHandler{nullptr};
        std::atomic<NetworkStatusHandler> applicationNetworkStatusHandler{nullptr};
        std::atomic<MessageHandler> applicationMessageHandler{nullptr};
        constexpr size_t NetworkLayerCount = 4;
        // Last state reported per layer. STOPPED is 0, the state every layer starts in.
        std::atomic<uint8_t> lastLayerState[NetworkLayerCount] = {};

        // ------------------------------------------------------------------
        // Filter tables. The application's transport filters follow the
        // transport; each connection's own filters stay on that connection.

        SemaphoreHandle_t filterMutex = nullptr;
        std::vector<std::string> transportFilters;
        std::vector<std::string> mqttFilters;
        std::vector<std::string> espNowFilters;

        bool ensureFilterMutex()
        {
            if (filterMutex == nullptr)
                filterMutex = xSemaphoreCreateMutex();
            return filterMutex != nullptr;
        }

        std::vector<std::string> *connectionFilters(ConnectionType connection)
        {
            switch (connection)
            {
            case ConnectionType::MQTT: return &mqttFilters;
            case ConnectionType::ESP_NOW: return &espNowFilters;
            case ConnectionType::GATEWAY:
            case ConnectionType::AUTO: break;
            }
            return nullptr;
        }

        // 1 added, 0 already present, -1 refused.
        int addFilter(std::vector<std::string> &table, const char *filter)
        {
            if (!ensureFilterMutex())
                return -1;
            xSemaphoreTake(filterMutex, portMAX_DELAY);
            int result = 0;
            if (std::find(table.begin(), table.end(), filter) == table.end())
            {
                if (table.size() >= NM_MAX_APP_SUBSCRIPTIONS)
                    result = -1;
                else
                {
                    table.emplace_back(filter);
                    result = 1;
                }
            }
            xSemaphoreGive(filterMutex);
            return result;
        }

        bool removeFilter(std::vector<std::string> &table, const char *filter)
        {
            if (!ensureFilterMutex())
                return false;
            xSemaphoreTake(filterMutex, portMAX_DELAY);
            const auto it = std::find(table.begin(), table.end(), filter);
            const bool found = it != table.end();
            if (found)
                table.erase(it);
            xSemaphoreGive(filterMutex);
            return found;
        }

        bool containsFilter(const std::vector<std::string> &table, const char *filter)
        {
            if (!ensureFilterMutex())
                return false;
            xSemaphoreTake(filterMutex, portMAX_DELAY);
            const bool found = std::find(table.begin(), table.end(), filter) != table.end();
            xSemaphoreGive(filterMutex);
            return found;
        }

        std::vector<std::string> copyFilters(const std::vector<std::string> &table)
        {
            std::vector<std::string> copy;
            if (!ensureFilterMutex())
                return copy;
            xSemaphoreTake(filterMutex, portMAX_DELAY);
            copy = table;
            xSemaphoreGive(filterMutex);
            return copy;
        }

        bool anyFilterMatches(const std::vector<std::string> &table, const std::string &topic)
        {
            if (!ensureFilterMutex())
                return false;
            xSemaphoreTake(filterMutex, portMAX_DELAY);
            bool match = false;
            for (const std::string &filter : table)
                if (TopicMatches(topic, filter))
                {
                    match = true;
                    break;
                }
            xSemaphoreGive(filterMutex);
            return match;
        }

        // ------------------------------------------------------------------
        // Drivers, addressed by connection.

        bool connectionConnected(ConnectionType connection)
        {
            switch (connection)
            {
            case ConnectionType::MQTT:
#if NM_ENABLE_MQTT
                return Mqtt_enabled() && Mqtt_state() == ConnectivityState::CONNECTED;
#else
                return false;
#endif
            case ConnectionType::ESP_NOW:
#if NM_NETWORK_ESPNOW
                return EspNow_enabled() && EspNow_state() == ConnectivityState::CONNECTED;
#else
                return false;
#endif
            case ConnectionType::GATEWAY:
#if NM_ENABLE_GATEWAY
                return GatewayNode::started();
#else
                return false;
#endif
            case ConnectionType::AUTO:
                return false;
            }
            return false;
        }

        bool driverPublish(ConnectionType connection, const char *topic, const uint8_t *payload,
                           size_t length, bool retained)
        {
            switch (connection)
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
            case ConnectionType::GATEWAY:
#if NM_ENABLE_GATEWAY
                return GatewayNode::publish(topic, payload, length, retained);
#else
                return false;
#endif
            case ConnectionType::AUTO:
                return false;
            }
            return false;
        }

        bool driverSubscribe(ConnectionType connection, const char *topicFilter)
        {
            switch (connection)
            {
            case ConnectionType::MQTT:
#if NM_ENABLE_MQTT
                return NmMqttConnection::subscribe(topicFilter);
#else
                return false;
#endif
            case ConnectionType::ESP_NOW:
#if NM_NETWORK_ESPNOW
                return NmEspNowConnection::subscribe(topicFilter);
#else
                return false;
#endif
            case ConnectionType::GATEWAY:
                // The gateway's broker already sees every message.
                return NM_ENABLE_GATEWAY != 0;
            case ConnectionType::AUTO:
                return false;
            }
            return false;
        }

        bool driverUnsubscribe(ConnectionType connection, const char *topicFilter)
        {
            switch (connection)
            {
            case ConnectionType::MQTT:
#if NM_ENABLE_MQTT
                return NmMqttConnection::unsubscribe(topicFilter);
#else
                return false;
#endif
            case ConnectionType::ESP_NOW:
#if NM_NETWORK_ESPNOW
                return NmEspNowConnection::unsubscribe(topicFilter);
#else
                return false;
#endif
            case ConnectionType::GATEWAY:
                return NM_ENABLE_GATEWAY != 0;
            case ConnectionType::AUTO:
                return false;
            }
            return false;
        }

        bool transportConnected()
        {
            return connectionState == ConnectionState::CONNECTED &&
                   selectedConnection != ConnectionType::AUTO;
        }

        // ------------------------------------------------------------------
        // Application event delivery: direct, or queued for DeliverEvents().

        enum class EventKind : uint8_t
        {
            MESSAGE,
            CONNECT,
            STATUS
        };

        void runMessageHandler(const ReceivedMessage &message)
        {
            const MessageHandler handler = applicationMessageHandler.load();
            if (handler != nullptr)
                handler(message);
        }

        void runConnectHandler()
        {
            const ConnectHandler handler = applicationConnectHandler.load();
            if (handler != nullptr)
                handler();
        }

        void runStatusHandler(const NetworkStatusEvent &event)
        {
            const NetworkStatusHandler handler = applicationNetworkStatusHandler.load();
            if (handler != nullptr)
                handler(event);
        }

        std::atomic<uint32_t> statQueued{0};
        std::atomic<uint32_t> statDelivered{0};
        std::atomic<uint32_t> statDropped{0};
        std::atomic<uint32_t> statHighWater{0};

#if NM_MESSAGE_QUEUE_DEPTH > 0
        // One allocation per event: header, then topic + NUL and payload + NUL.
        struct QueuedEvent
        {
            EventKind kind;
            NetworkStatusEvent status;
            uint32_t topicLength;
            uint32_t payloadLength;
            ConnectionType source;
            bool retained;
            bool transport;
            char *topic() { return reinterpret_cast<char *>(this + 1); }
            uint8_t *payload() { return reinterpret_cast<uint8_t *>(topic() + topicLength + 1); }
        };

        QueueHandle_t eventQueue = nullptr;

        bool ensureEventQueue()
        {
            if (eventQueue == nullptr)
                eventQueue = xQueueCreate(NM_MESSAGE_QUEUE_DEPTH, sizeof(QueuedEvent *));
            return eventQueue != nullptr;
        }

        QueuedEvent *allocateEvent(size_t extra)
        {
            void *memory = heap_caps_malloc(sizeof(QueuedEvent) + extra,
                                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (memory == nullptr)
                memory = heap_caps_malloc(sizeof(QueuedEvent) + extra, MALLOC_CAP_8BIT);
            return static_cast<QueuedEvent *>(memory);
        }

        void enqueue(QueuedEvent *event)
        {
            if (event == nullptr || !ensureEventQueue() || xQueueSend(eventQueue, &event, 0) != pdTRUE)
            {
                if (event != nullptr)
                    heap_caps_free(event);
                statDropped.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            statQueued.fetch_add(1, std::memory_order_relaxed);
            const uint32_t depth = uxQueueMessagesWaiting(eventQueue);
            uint32_t high = statHighWater.load(std::memory_order_relaxed);
            while (depth > high && !statHighWater.compare_exchange_weak(high, depth))
            {
            }
        }
#endif

        void emitMessage(const ReceivedMessage &message)
        {
            if (applicationMessageHandler.load() == nullptr)
                return;
#if NM_MESSAGE_QUEUE_DEPTH > 0
            const size_t topicLength = strlen(message.topic);
            QueuedEvent *event = allocateEvent(topicLength + 1 + message.length + 1);
            if (event != nullptr)
            {
                event->kind = EventKind::MESSAGE;
                event->topicLength = static_cast<uint32_t>(topicLength);
                event->payloadLength = static_cast<uint32_t>(message.length);
                event->source = message.source;
                event->retained = message.retained;
                event->transport = message.transport;
                memcpy(event->topic(), message.topic, topicLength + 1);
                if (message.length != 0)
                    memcpy(event->payload(), message.payload, message.length);
                event->payload()[message.length] = 0;
            }
            enqueue(event);
#else
            statQueued.fetch_add(1, std::memory_order_relaxed);
            runMessageHandler(message);
            statDelivered.fetch_add(1, std::memory_order_relaxed);
#endif
        }

        void emitConnect()
        {
            if (applicationConnectHandler.load() == nullptr)
                return;
#if NM_MESSAGE_QUEUE_DEPTH > 0
            QueuedEvent *event = allocateEvent(2);
            if (event != nullptr)
            {
                event->kind = EventKind::CONNECT;
                event->topicLength = 0;
                event->payloadLength = 0;
            }
            enqueue(event);
#else
            runConnectHandler();
#endif
        }

        void emitStatus(const NetworkStatusEvent &status)
        {
            if (applicationNetworkStatusHandler.load() == nullptr)
                return;
#if NM_MESSAGE_QUEUE_DEPTH > 0
            QueuedEvent *event = allocateEvent(2);
            if (event != nullptr)
            {
                event->kind = EventKind::STATUS;
                event->status = status;
                event->topicLength = 0;
                event->payloadLength = 0;
            }
            enqueue(event);
#else
            runStatusHandler(status);
#endif
        }

        // ------------------------------------------------------------------
        // Network layer reporting.

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

        // Compares every layer with what was last reported and emits each that
        // moved. The exchange makes each transition reported exactly once even
        // when two tasks get here at the same time.
        void reportNetworkStatus()
        {
            for (size_t i = 0; i < NetworkLayerCount; ++i)
            {
                const NetworkLayer layer = static_cast<NetworkLayer>(i);
                const ConnectivityState state = currentLayerState(layer);
                const ConnectivityState previous = static_cast<ConnectivityState>(
                    lastLayerState[i].exchange(static_cast<uint8_t>(state)));
                if (state == previous)
                    continue;
                const NetworkStatusEvent event = {
                    layer, state, previous,
                    state == (layer == NetworkLayer::WIFI_RADIO ? ConnectivityState::READY
                                                                : ConnectivityState::CONNECTED)};
                emitStatus(event);
            }
        }

        // ------------------------------------------------------------------
        // PublishTextWhenConnected() backlog, flushed on every connect whatever
        // the connection. Guarded by deferredMutex.

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

        // ------------------------------------------------------------------
        // Routing.

        bool connectionCompiled(ConnectionType connection)
        {
            switch (connection)
            {
            case ConnectionType::MQTT:
                return NM_ENABLE_MQTT != 0;
            case ConnectionType::ESP_NOW:
                return NM_NETWORK_ESPNOW != 0;
            case ConnectionType::GATEWAY:
                return NM_ENABLE_GATEWAY != 0;
            case ConnectionType::AUTO:
                return false;
            }
            return false;
        }

        // Connected and able to carry presence. MQTT carries the device's
        // status Last Will only while it may hold presence; until it has
        // reconnected with one it is not a transport.
        bool transportUsable(ConnectionType connection)
        {
            if (!connectionConnected(connection))
                return false;
#if NM_ENABLE_MQTT
            if (connection == ConnectionType::MQTT)
                return NmMqttConnection::presenceReady();
#endif
            return true;
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
                return TransportSubscribe(topicFilter.c_str());
            }

            bool unsubscribe(const std::string &topicFilter) override
            {
                return TransportUnsubscribe(topicFilter.c_str());
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

        void installFilters(ConnectionType connection, const std::vector<std::string> &filters)
        {
            for (const std::string &filter : filters)
                if (!driverSubscribe(connection, filter.c_str()))
                    LOG_WARNING("NET", "Subscription %s on %s was not accepted", filter.c_str(),
                                ConnectionTypeName(connection));
        }

        void notifyConnectedOwners()
        {
            gResourcesManager.subscribeAll();
            GatewayCandidateOnConnected();
            NmMessageRouter::onConnected();
            installFilters(selectedConnection, copyFilters(transportFilters));
            emitConnect();
            flushDeferred();
        }

        void requestNetworkTelemetry()
        {
#if NM_ENABLE_TELEMETRY
            SystemState.request(SystemRequest::PublishTelemetry);
#endif
        }

        // Presence follows the transport: each connection installs or drops
        // its Last Will on <device>/status from here.
        void notifyRoutingChanged()
        {
#if NM_ENABLE_MQTT
            NmMqttConnection::onRoutingChanged();
#endif
#if NM_NETWORK_ESPNOW
            NmEspNowConnection::onRoutingChanged();
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
            const bool changed = connection != previous || connectionState != previousState;
            if (coordinatorBegun && connection != ConnectionType::AUTO && changed)
                notifyConnectedOwners();
            if (changed)
            {
                notifyRoutingChanged();
                requestNetworkTelemetry();
            }
        }

        void reevaluateRouting(ConnectionType preference)
        {
#if NM_ENABLE_GATEWAY
            // A gateway is its own transport; there is nothing to choose.
            (void)preference;
            activate(connectionConnected(ConnectionType::GATEWAY) ? ConnectionType::GATEWAY
                                                                  : ConnectionType::AUTO);
            return;
#endif
            const ConnectionType current = static_cast<ConnectionType>(selectedConnection);
            activate(ResolveNetworkRoute(preference, current,
                                         transportUsable(ConnectionType::MQTT),
                                         transportUsable(ConnectionType::ESP_NOW),
                                         routeEligible(ConnectionType::ESP_NOW)));
        }

        // A write of preferredConnection runs its handler before the Config
        // holds the new value; routing and presence must already see it.
        volatile int preferenceInEffect = -1;

        ConnectionType effectivePreference()
        {
            const int pending = preferenceInEffect;
            return static_cast<ConnectionType>(pending >= 0 ? pending : preferredConnection.value());
        }

        void reevaluateRouting()
        {
            reevaluateRouting(effectivePreference());
        }

        bool changePreferredConnection(Config<int> &, const int &requested)
        {
            const ConnectionType connection = static_cast<ConnectionType>(requested);
            if (connection != ConnectionType::AUTO && !connectionCompiled(connection))
                return false;
            preferenceInEffect = requested;
            reevaluateRouting(connection);
            // The preference decides whether MQTT keeps a Last Will while
            // ESP-NOW carries the traffic.
            notifyRoutingChanged();
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

    // -----------------------------------------------------------------------
    // Transport API.

    bool Publish(const char *topic, const uint8_t *payload, size_t length, bool retained)
    {
        if (topic == nullptr || topic[0] == '\0' ||
            (payload == nullptr && length != 0) || !transportConnected())
            return false;
        return driverPublish(selectedConnection, topic, payload, length, retained);
    }

    bool PublishText(const std::string &topic, const std::string &payload, bool retained)
    {
        return Publish(topic.c_str(),
                       reinterpret_cast<const uint8_t *>(payload.c_str()),
                       payload.length(), retained);
    }

    bool Subscribe(const char *topicFilter)
    {
        if (!ValidTopicFilter(topicFilter))
            return false;
        const int added = addFilter(transportFilters, topicFilter);
        if (added < 0)
            return false;
        if (added > 0 && transportConnected())
            driverSubscribe(selectedConnection, topicFilter);
        return true;
    }

    bool Unsubscribe(const char *topicFilter)
    {
        if (!ValidTopicFilter(topicFilter))
            return false;
        if (removeFilter(transportFilters, topicFilter) && transportConnected())
            driverUnsubscribe(selectedConnection, topicFilter);
        return true;
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

    // -----------------------------------------------------------------------
    // Connection API.

    bool ConnectionUp(ConnectionType connection)
    {
        return connectionConnected(connection);
    }

    bool PublishOn(ConnectionType connection, const char *topic, const uint8_t *payload,
                   size_t length, bool retained)
    {
        if (topic == nullptr || topic[0] == '\0' || (payload == nullptr && length != 0) ||
            !connectionConnected(connection))
            return false;
        return driverPublish(connection, topic, payload, length, retained);
    }

    bool PublishTextOn(ConnectionType connection, const std::string &topic,
                       const std::string &payload, bool retained)
    {
        return PublishOn(connection, topic.c_str(),
                         reinterpret_cast<const uint8_t *>(payload.c_str()),
                         payload.length(), retained);
    }

    bool SubscribeOn(ConnectionType connection, const char *topicFilter)
    {
        std::vector<std::string> *table = connectionFilters(connection);
        if (table == nullptr || !connectionCompiled(connection) || !ValidTopicFilter(topicFilter))
            return false;
        const int added = addFilter(*table, topicFilter);
        if (added < 0)
            return false;
        if (added > 0 && connectionConnected(connection))
            driverSubscribe(connection, topicFilter);
        return true;
    }

    bool UnsubscribeOn(ConnectionType connection, const char *topicFilter)
    {
        std::vector<std::string> *table = connectionFilters(connection);
        if (table == nullptr || !ValidTopicFilter(topicFilter))
            return false;
        if (!removeFilter(*table, topicFilter) || !connectionConnected(connection))
            return true;
        // The same filter may still be wanted on the transport.
        const bool stillWanted = connection == selectedConnection && transportConnected() &&
                                 containsFilter(transportFilters, topicFilter);
        if (!stillWanted)
            driverUnsubscribe(connection, topicFilter);
        return true;
    }

    // -----------------------------------------------------------------------
    // Delivery.

    void DeliverEvents(uint32_t budgetMs)
    {
#if NM_MESSAGE_QUEUE_DEPTH > 0
        if (eventQueue == nullptr)
            return;
        const uint32_t start = Platform::monotonicMillis();
        QueuedEvent *event = nullptr;
        while (xQueueReceive(eventQueue, &event, 0) == pdTRUE)
        {
            switch (event->kind)
            {
            case EventKind::MESSAGE:
            {
                const ReceivedMessage message = {event->topic(), event->payload(),
                                                 event->payloadLength, event->retained,
                                                 event->source, event->transport};
                runMessageHandler(message);
                break;
            }
            case EventKind::CONNECT:
                runConnectHandler();
                break;
            case EventKind::STATUS:
                runStatusHandler(event->status);
                break;
            }
            heap_caps_free(event);
            statDelivered.fetch_add(1, std::memory_order_relaxed);
            if (budgetMs != 0 && Platform::monotonicMillis() - start >= budgetMs)
                break;
        }
#else
        (void)budgetMs;
#endif
    }

    DeliveryStats GetDeliveryStats()
    {
        return {statQueued.load(), statDelivered.load(), statDropped.load(), statHighWater.load()};
    }

    // -----------------------------------------------------------------------
    // Routing state.

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
        if (connection != ConnectionType::AUTO && !connectionCompiled(connection))
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
        return effectivePreference();
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
        case ConnectionType::GATEWAY: return "GATEWAY";
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

    // -----------------------------------------------------------------------
    // Driver ingress.

    void OnConnectedIngress(ConnectionType connection)
    {
        // The connection's own filters come back with every session of it,
        // whichever connection is the transport.
        if (std::vector<std::string> *table = connectionFilters(connection))
            installFilters(connection, copyFilters(*table));
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

    void OnMessageIngress(ConnectionType source, const std::string &topic,
                          const std::string &payload, bool retained)
    {
        if (source == selectedConnection && transportConnected())
        {
            NmMessageRouter::handleMessage(topic, payload, retained);
            return;
        }
        // Not the transport: only what the application asked of this
        // connection gets through. Filters left over from when it was the
        // transport do not.
        const std::vector<std::string> *table = connectionFilters(source);
        if (table == nullptr || !anyFilterMatches(*table, topic))
            return;
        const ReceivedMessage message = {topic.c_str(),
                                         reinterpret_cast<const uint8_t *>(payload.c_str()),
                                         payload.length(), retained, source, false};
        emitMessage(message);
    }

#if NM_ENABLE_GATEWAY
    void OnGatewayIngress(const std::string &topic, const std::string &payload, bool retained,
                          bool &consumed)
    {
        consumed = false;
        if (selectedConnection != ConnectionType::GATEWAY || !transportConnected())
            return;
        NmMessageRouter::handleMessage(topic, payload, retained, &consumed);
    }
#endif

    void ConnectionTick()
    {
        reevaluateRouting();
#if NM_MESSAGE_QUEUE_DEPTH > 0 && NM_DELIVER_IN_TICK
        DeliverEvents(0);
#endif
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

    bool TransportSubscribe(const char *topicFilter)
    {
        if (!ValidTopicFilter(topicFilter) || !transportConnected())
            return false;
        return driverSubscribe(selectedConnection, topicFilter);
    }

    bool TransportUnsubscribe(const char *topicFilter)
    {
        if (!ValidTopicFilter(topicFilter) || !transportConnected())
            return false;
        return driverUnsubscribe(selectedConnection, topicFilter);
    }

    bool MqttMayHoldPresence()
    {
        // MQTT gives up its Last Will only while ESP-NOW carries the traffic
        // and would keep doing so: with MQTT preferred, MQTT takes the route
        // back as soon as it is usable, so it must already hold presence.
        return !(selectedConnection == ConnectionType::ESP_NOW && transportConnected() &&
                 effectivePreference() != ConnectionType::MQTT);
    }

    bool DispatchApplicationMessage(const char *topic, const uint8_t *payload,
                                    size_t length, bool retained)
    {
        if (applicationMessageHandler.load() == nullptr)
            return false;
        // A gateway's broker carries the whole network; the application gets
        // what it subscribed to, as it would from a broker.
        if (selectedConnection == ConnectionType::GATEWAY &&
            !anyFilterMatches(transportFilters, topic))
            return true;
        const ReceivedMessage message = {topic, payload, length, retained,
                                         static_cast<ConnectionType>(selectedConnection), true};
        emitMessage(message);
        return true;
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

    std::string ConnectionDeviceStatusJson(const std::string &deviceName, bool online,
                                           ConnectionType via)
    {
        JsonDocument doc;
        // The current identity reads as its label; a former address (identity
        // cleanup) only has its own name.
        const bool current = deviceName == gDeviceIdentity.getDeviceName();
        doc["name"] = current ? gDeviceIdentity.getDisplayName() : deviceName;
        doc["address"] = deviceName;
        doc["hardware"] = gDeviceIdentity.getHardwareSignature();
        doc["timezone"] = gDeviceIdentity.getTimezone();
        doc["online"] = online;
        if (via != ConnectionType::AUTO)
        {
            JsonObject route = doc["via"].to<JsonObject>();
            route["transport"] = ConnectionTypeName(via);
#if NM_ENABLE_MQTT
            if (via == ConnectionType::MQTT)
                route["profile"] = MqttProfileName(Mqtt_profile());
#endif
#if NM_NETWORK_ESPNOW
            if (via == ConnectionType::ESP_NOW)
            {
                const std::string gateway = EspNowClient::gatewayId();
                if (!gateway.empty())
                    route["gateway"] = gateway;
            }
#endif
        }
        std::string payload;
        serializeJson(doc, payload);
        return payload;
    }

    std::string ConnectionDeviceStatusJson(bool online, ConnectionType via)
    {
        return ConnectionDeviceStatusJson(gDeviceIdentity.getDeviceName(), online, via);
    }

    std::string ConnectionDeviceStatusJson(bool online)
    {
        return ConnectionDeviceStatusJson(online, GetActiveConnection());
    }
}

#endif // NM_ENABLE_NETWORK
