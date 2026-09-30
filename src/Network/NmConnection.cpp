#include <NightMare/Features.h>
#if NM_ENABLE_NETWORK

#include "NmConnection.h"
#include "NmConnectionInternal.h"

#include <Core/ConfigManager.h>
#include <Core/DeviceIdentity.h>
#include <Core/Logs.h>
#include <Core/ResourcesManager.h>
#include <Network/NmMessageRouter.h>
#if NM_ENABLE_MQTT
#include <Network/MQTT/NmMqttConnection.h>
#endif
#if NM_NETWORK_ESPNOW
#include <Network/EspNow/NmEspNowConnection.h>
#endif
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace NightMare
{
namespace
{
constexpr ConnectionType defaultConnection()
{
#if NM_NETWORK_MQTT
    return ConnectionType::MQTT;
#elif NM_NETWORK_LOCALMQTT
    return ConnectionType::LOCAL_MQTT;
#elif NM_NETWORK_ESPNOW
    return ConnectionType::ESP_NOW;
#else
    return ConnectionType::AUTO;
#endif
}
}

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
// What the drivers below have reported; see NmConnectionInternal.h.
bool radioAvailable = false;
bool ipLinkAvailable = false;
ConnectionType rollbackConnection = ConnectionType::AUTO;
bool rollbackAvailable = false;

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

bool isMqtt(ConnectionType connection)
{
    return connection == ConnectionType::MQTT ||
           connection == ConnectionType::LOCAL_MQTT;
}

bool connectionEnabled(ConnectionType connection)
{
    switch (connection)
    {
    case ConnectionType::MQTT:
        return NM_NETWORK_MQTT != 0;
    case ConnectionType::LOCAL_MQTT:
        return NM_NETWORK_LOCALMQTT != 0;
    case ConnectionType::ESP_NOW:
        return NM_NETWORK_ESPNOW != 0;
    case ConnectionType::AUTO:
        return false;
    }
    return false;
}

// Whether what a connection runs on is there yet:
//   MQTT, LOCAL_MQTT   radio + an IP link (a joined AP with an address)
//   ESP_NOW            the radio alone -- no AP, no IP
bool requirementsMet(ConnectionType connection)
{
    switch (connection)
    {
    case ConnectionType::MQTT:
    case ConnectionType::LOCAL_MQTT:
        return ipLinkAvailable;
    case ConnectionType::ESP_NOW:
        return radioAvailable;
    case ConnectionType::AUTO:
        return false;
    }
    return false;
}

const char *requirementName(ConnectionType connection)
{
    return connection == ConnectionType::ESP_NOW ? "the Wi-Fi radio" : "an IP link";
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
    return isMqtt(static_cast<ConnectionType>(selectedConnection)) &&
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
    return isMqtt(static_cast<ConnectionType>(selectedConnection)) &&
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

#if NM_NETWORK_ESPNOW
bool startEspNow()
{
    if (selectedConnection == ConnectionType::ESP_NOW &&
        (connectionState == ConnectionState::CONNECTING ||
         connectionState == ConnectionState::CONNECTED))
        return true;
    // esp_now_init needs a started driver. Without it, wait: the radio
    // ingress starts ESP-NOW as soon as it comes up.
    if (!radioAvailable)
    {
        LOG("NET", "ESP-NOW waits for the Wi-Fi radio");
        return false;
    }

    attachResourceConnection();
    registerFrameworkSubscriptions();

    const ConnectionType previousConnection = selectedConnection;
    const ConnectionState previousState = connectionState;
#if NM_ENABLE_MQTT
    // One connection at a time: stop the MQTT client before ESP-NOW takes over.
    if (isMqtt(previousConnection) && NmMqttConnection::state() != -1)
        NmMqttConnection::end();
#endif
    // No rollback across drivers: a failed ESP-NOW start is reported as ERROR.
    rollbackAvailable = false;
    selectedConnection = ConnectionType::ESP_NOW;
    connectionState = ConnectionState::CONNECTING;
    if (NmEspNowConnection::begin())
        return true;
    selectedConnection = previousConnection;
    connectionState = previousState;
    return false;
}
#endif

bool startConnection(ConnectionType connection)
{
    if (!connectionEnabled(connection))
        return false;
#if NM_NETWORK_ESPNOW
    if (connection == ConnectionType::ESP_NOW)
        return startEspNow();
    if (NmEspNowConnection::running())
        NmEspNowConnection::end();
#endif
    if (!isMqtt(connection))
        return false;
#if NM_ENABLE_MQTT
    if (selectedConnection == connection &&
        (connectionState == ConnectionState::CONNECTING ||
         connectionState == ConnectionState::CONNECTED))
        return true;

    attachResourceConnection();
    registerFrameworkSubscriptions();

    const ConnectionType previousConnection = selectedConnection;
    const ConnectionState previousState = connectionState;
    const ConnectionType previousRollbackConnection = rollbackConnection;
    const bool previousRollbackAvailable = rollbackAvailable;
    rollbackAvailable = previousState == ConnectionState::CONNECTED &&
                        isMqtt(previousConnection) && previousConnection != connection;
    if (rollbackAvailable)
        rollbackConnection = previousConnection;
    selectedConnection = connection;
    connectionState = ConnectionState::CONNECTING;

    const bool accepted = NmMqttConnection::state() == -1
                              ? NmMqttConnection::begin(connection)
                              : NmMqttConnection::changeTo(connection);
    if (!accepted)
    {
        selectedConnection = previousConnection;
        connectionState = previousState;
        rollbackConnection = previousRollbackConnection;
        rollbackAvailable = previousRollbackAvailable;
    }
    return accepted;
#else
    (void)connection;
    return false;
#endif
}

// Start policy: the persisted preference first, then the build's default
// profile. Used whenever the radio or the IP link comes up and nothing runs.
//
// A preference this build supports but can't run *yet* is waited for, not
// skipped: with MQTT preferred and ESP-NOW also compiled in, the radio comes
// up before the IP link, and falling back then would leave ESP-NOW running
// and MQTT never started. The fallback is for a preference this build lacks
// (e.g. one persisted by older firmware) or one that failed to start.
bool startPreferredConnection()
{
    const ConnectionType preferred = static_cast<ConnectionType>(preferredConnection.value());
    if (connectionEnabled(preferred))
    {
        if (!requirementsMet(preferred))
            return false;
        if (startConnection(preferred))
            return true;
    }
    const ConnectionType fallback = defaultConnection();
    return fallback != preferred && connectionEnabled(fallback) && requirementsMet(fallback) &&
           startConnection(fallback);
}

bool nothingRunning()
{
    return connectionState == ConnectionState::STOPPED || connectionState == ConnectionState::ERROR;
}

bool changePreferredConnection(Config<int> &, const int &requested)
{
    const ConnectionType connection = static_cast<ConnectionType>(requested);
    return connectionEnabled(connection) && startConnection(connection);
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
    case ConnectionType::LOCAL_MQTT:
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

bool SelectConnection(ConnectionType connection)
{
    if (!connectionEnabled(connection))
        return false;

    String request = "set ";
    request += preferredConnection.name();
    request += ' ';
    request += String(static_cast<int>(connection));
    return configManager().handle(request) == "OK";
}

ConnectionType GetSelectedConnection()
{
    return static_cast<ConnectionType>(selectedConnection);
}

ConnectionState GetConnectionState()
{
    return static_cast<ConnectionState>(connectionState);
}

void OnConnectedIngress(ConnectionType connection)
{
    if (connection != selectedConnection)
        return;
    connectionState = ConnectionState::CONNECTED;
    rollbackAvailable = false;
    restoreSubscriptions();
    NmMessageRouter::onConnected();
    flushDeferred();
}

void OnDisconnectedIngress(ConnectionType connection)
{
    if (connection == selectedConnection && connectionState != ConnectionState::STOPPED)
        connectionState = ConnectionState::CONNECTING;
}

void OnConnectionFailedIngress(ConnectionType connection)
{
    if (connection != selectedConnection)
        return;

    if (!rollbackAvailable)
    {
        connectionState = ConnectionState::ERROR;
        return;
    }

    const ConnectionType fallback = rollbackConnection;
    rollbackAvailable = false;
    LOG_WARNING("NET", "Connection type %u failed; rolling back to %u",
                static_cast<unsigned>(connection), static_cast<unsigned>(fallback));

    if (!preferredConnection.set(static_cast<int>(fallback)))
        LOG_ERROR("NET", "Could not persist the rollback connection");
    if (!startConnection(fallback))
        connectionState = ConnectionState::ERROR;
}

bool ConnectionBegin()
{
    if (!nothingRunning())
        return true;
    // Whatever can run now starts now. Anything still waiting on the radio or
    // an IP link starts from the matching ingress when that arrives.
    if (startPreferredConnection())
        return true;
    ConnectionType wanted = static_cast<ConnectionType>(preferredConnection.value());
    if (!connectionEnabled(wanted))
        wanted = defaultConnection();
    if (connectionEnabled(wanted) && !requirementsMet(wanted))
        LOG("NET", "Connection %u waits for %s", static_cast<unsigned>(wanted),
            requirementName(wanted));
    return false;
}

void OnRadioAvailabilityIngress(bool available)
{
    radioAvailable = available;
    if (!available)
    {
#if NM_NETWORK_ESPNOW
        // ESP-NOW runs on the radio. The radio reports going down before it
        // stops, so this ends ESP-NOW while the driver still works, and back
        // to STOPPED lets the next radio-up start it again.
        if (selectedConnection == ConnectionType::ESP_NOW && NmEspNowConnection::running())
        {
            NmEspNowConnection::end();
            connectionState = ConnectionState::STOPPED;
        }
#endif
        return;
    }
    if (nothingRunning())
        startPreferredConnection();
}

void OnIpLinkAvailabilityIngress(bool available)
{
    ipLinkAvailable = available;
    if (!available)
        return;
    // An IP link implies a running radio, whatever did or didn't report it.
    radioAvailable = true;
    if (!nothingRunning())
        return;
    // No enabled profile means there is nothing to start, which is not an error.
    if (!startPreferredConnection() && defaultConnection() != ConnectionType::AUTO)
        LOG_ERROR("NET", "Could not start the preferred connection");
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
