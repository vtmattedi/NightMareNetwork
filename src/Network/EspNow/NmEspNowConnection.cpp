#include <NightMare/Features.h>
#if NM_NETWORK_ESPNOW

#include "NmEspNowConnection.h"
#include "EspNowClient.h"

#include <Core/Config.h>
#include <Core/DeviceIdentity.h>
#include <Core/Logs.h>
#include <Network/NmMessageRouter.h>
#include <Network/NmConnectionInternal.h>
#include <Network/WiFiRadio/NmWifiRadioService.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace
{
constexpr uint8_t QueueDepth = 8;
constexpr uint32_t WorkerStackBytes = 6144;

// Everything the client reports is handled on the worker: the CONNECTED event
// lets each logical owner reinstall subscriptions and announces status, and a send
// blocks until the send callback, which runs on the same task that delivers
// receive callbacks.
struct Event
{
    bool isMessage = false;
    NightMare::EspNowClient::State state = NightMare::EspNowClient::State::STOPPED;
    std::string topic;
    std::string payload;
    bool retained = false;
};

QueueHandle_t events = nullptr;
TaskHandle_t worker = nullptr;
volatile bool workerRun = false;
volatile bool workerExited = true;
bool connectedReported = false;
volatile bool serviceEnabled = false;
volatile bool serviceSuspended = false;
volatile NightMare::ConnectivityState serviceState = NightMare::ConnectivityState::STOPPED;

void serviceChanged()
{
    NightMare::OnConnectivityStateChanged();
}

void post(Event *event)
{
    if (events == nullptr || xQueueSend(events, &event, 0) != pdTRUE)
        delete event; // the client will resend state on the next change; messages are best effort
}

void onState(NightMare::EspNowClient::State state)
{
    Event *event = new Event();
    event->state = state;
    post(event);
}

void onMessage(const char *topic, const uint8_t *payload, size_t length, bool retained)
{
    Event *event = new Event();
    event->isMessage = true;
    event->topic = topic;
    event->retained = retained;
    event->payload.reserve(length);
    for (size_t i = 0; i < length; ++i)
        event->payload += static_cast<char>(payload[i]);
    post(event);
}

// The gateway publishes this when the device goes silent (300 s), the same
// role the MQTT last will plays. Only the transport holds presence: while
// another connection carries the traffic the gateway keeps no will for this
// device, so losing ESP-NOW then cannot mark a device offline that is still
// up on MQTT. Only resent when it changed.
std::string currentWill;
SemaphoreHandle_t willLock = nullptr;

void applyLastWill()
{
    if (willLock == nullptr)
        willLock = xSemaphoreCreateMutex();
    if (willLock == nullptr)
        return;
    xSemaphoreTake(willLock, portMAX_DELAY);
    const bool holdsPresence =
        NightMare::GetActiveConnection() == NightMare::ConnectionType::ESP_NOW &&
        NightMare::GetConnectionState() == NightMare::ConnectionState::CONNECTED;
    if (!holdsPresence)
    {
        if (!currentWill.empty())
        {
            NightMare::EspNowClient::clearLastWill();
            currentWill.clear();
        }
        xSemaphoreGive(willLock);
        return;
    }
    const std::string offline =
        NightMare::ConnectionDeviceStatusJson(false, NightMare::ConnectionType::ESP_NOW);
    if (offline != currentWill)
    {
        const std::string willTopic = gDeviceIdentity.topic("status");
        if (NightMare::EspNowClient::setLastWill(willTopic.c_str(),
                                                 reinterpret_cast<const uint8_t *>(offline.c_str()),
                                                 offline.length(), true))
            currentWill = offline;
        else
            LOG_WARNING("ESPNOW", "Could not set the last will");
    }
    xSemaphoreGive(willLock);
}

void handle(const Event &event)
{
    using NightMare::EspNowClient::State;
    if (!event.isMessage)
        LOG("ESPNOW", "Gateway state: %s", NightMare::EspNowClient::stateName(event.state));
    if (event.isMessage)
    {
        NightMare::OnMessageIngress(NightMare::ConnectionType::ESP_NOW, event.topic,
                                    event.payload, event.retained);
        return;
    }
    if (event.state == State::CONNECTED)
    {
        // The will follows routing (applyLastWill from onRoutingChanged), so
        // it is rebuilt once this session becomes the transport, with the
        // gateway id it reports.
        connectedReported = true;
        serviceState = NightMare::ConnectivityState::CONNECTED;
        NightMare::OnConnectedIngress(NightMare::ConnectionType::ESP_NOW);
        serviceChanged();
    }
    else
    {
        if (connectedReported)
        {
            connectedReported = false;
            NightMare::OnDisconnectedIngress(NightMare::ConnectionType::ESP_NOW);
        }
        if (serviceEnabled && !serviceSuspended)
            serviceState = event.state == State::STOPPED
                               ? NightMare::ConnectivityState::ERROR
                               : NightMare::ConnectivityState::CONNECTING;
        serviceChanged();
    }
}

void workerTask(void *)
{
    while (workerRun)
    {
        Event *event = nullptr;
        if (xQueueReceive(events, &event, pdMS_TO_TICKS(200)) != pdTRUE)
            continue;
        handle(*event);
        delete event;
    }
    workerExited = true;
    worker = nullptr;
    vTaskDelete(nullptr);
}

void drain()
{
    Event *event = nullptr;
    while (events != nullptr && xQueueReceive(events, &event, 0) == pdTRUE)
        delete event;
}
}

namespace NmEspNowConnection
{
bool begin()
{
    if (workerRun)
        return true;

    gDeviceIdentity.begin();
    gDeviceIdentity.lockAddress();
    if (events == nullptr)
        events = xQueueCreate(QueueDepth, sizeof(Event *));
    if (events == nullptr)
        return false;
    drain();
    connectedReported = false;

    // A new client starts without a will; routing installs one when ESP-NOW
    // becomes the transport.
    NightMare::EspNowClient::clearLastWill();
    currentWill.clear();

    NightMare::EspNowClient::onState(onState);
    NightMare::EspNowClient::onMessage(onMessage);

    workerRun = true;
    workerExited = false;
    if (xTaskCreate(workerTask, "espnow_conn", WorkerStackBytes, nullptr, 3, &worker) != pdPASS)
    {
        workerRun = false;
        workerExited = true;
        return false;
    }
    if (!NightMare::EspNowClient::begin())
    {
        LOG_ERROR("ESPNOW", "Client did not start (see the ESPNOW log above)");
        end();
        return false;
    }
    LOG("ESPNOW", "Started; searching for the gateway");
    return true;
}

void end()
{
    NightMare::EspNowClient::end();
    NightMare::EspNowClient::onState(nullptr);
    NightMare::EspNowClient::onMessage(nullptr);
    workerRun = false;
    for (int i = 0; i < 100 && !workerExited; ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    drain();
}

bool running()
{
    return workerRun;
}

void onRoutingChanged()
{
    if (workerRun)
        applyLastWill();
}

bool enabled() { return serviceEnabled; }
NightMare::ConnectivityState connectivityState() { return serviceState; }

bool suspend(NightMare::ConnectivitySuspendReason reason)
{
    if (reason != NightMare::ConnectivitySuspendReason::WIFI_SCAN || !serviceEnabled)
        return false;
    if (serviceSuspended)
        return true;
    serviceSuspended = true;
    serviceState = NightMare::ConnectivityState::SUSPENDED;
    serviceChanged();
    end();
    return true;
}

bool resume(NightMare::ConnectivitySuspendReason reason)
{
    if (reason != NightMare::ConnectivitySuspendReason::WIFI_SCAN || !serviceEnabled)
        return false;
    if (!serviceSuspended)
        return true;
    serviceSuspended = false;
    serviceState = NightMare::ConnectivityState::STARTING;
    if (!begin())
    {
        serviceState = NightMare::ConnectivityState::ERROR;
        serviceChanged();
        return false;
    }
    serviceState = NightMare::ConnectivityState::CONNECTING;
    serviceChanged();
    return true;
}

bool publish(const char *topic, const uint8_t *payload, size_t length, bool retained)
{
    return NightMare::EspNowClient::publish(topic, payload, length, retained);
}

bool subscribe(const char *topicFilter)
{
    return NightMare::EspNowClient::subscribe(topicFilter);
}

bool unsubscribe(const char *topicFilter)
{
    return NightMare::EspNowClient::unsubscribe(topicFilter);
}
}

namespace
{
bool applyEnable();
bool applyDisable();

bool writeEnabled(Config<bool> &, const bool &requested)
{
    return requested ? applyEnable() : applyDisable();
}
}

//@NightMare:Config ESP-NOW enabled
// Saved by EspNow_enable()/EspNow_disable() and restored at startup. A CONFIG
// SET applies it at once.
static Config<bool> espNowEnabledSetting("nightmare:connection:espnow_enabled", NM_ESPNOW_AUTO != 0);

namespace
{
struct SettingHandlerInstaller
{
    SettingHandlerInstaller() { espNowEnabledSetting.onWrite = writeEnabled; }
};
SettingHandlerInstaller settingHandlerInstaller;

void saveEnabled(bool enabled)
{
    if (espNowEnabledSetting.value() != enabled && !espNowEnabledSetting.set(enabled))
        LOG_WARNING("ESPNOW", "Could not save the ESP-NOW enable state");
}

bool applyEnable()
{
    using namespace NightMare;
    if (serviceEnabled)
        return true;
    if (WiFiRadio_state() != ConnectivityState::READY)
        return false;
    serviceEnabled = true;
    serviceSuspended = false;
    serviceState = ConnectivityState::STARTING;
    if (!NmEspNowConnection::begin())
    {
        serviceEnabled = false;
        serviceState = ConnectivityState::ERROR;
        serviceChanged();
        return false;
    }
    serviceState = ConnectivityState::CONNECTING;
    serviceChanged();
    return true;
}

bool applyDisable()
{
    using namespace NightMare;
    if (!serviceEnabled)
        return true;
    serviceEnabled = false;
    serviceSuspended = false;
    serviceState = ConnectivityState::STOPPED;
    OnDisconnectedIngress(ConnectionType::ESP_NOW);
    serviceChanged();
    NmEspNowConnection::end();
    return true;
}
}

namespace NightMare
{
bool EspNow_enable()
{
    if (!applyEnable())
        return false;
    saveEnabled(true);
    return true;
}

bool EspNow_disable()
{
    if (!applyDisable())
        return false;
    saveEnabled(false);
    return true;
}

bool EspNow_savedEnabled() { return espNowEnabledSetting.value(); }
bool EspNow_enabled() { return serviceEnabled; }
ConnectivityState EspNow_state() { return serviceState; }
bool EspNow_suspend(ConnectivitySuspendReason reason) { return NmEspNowConnection::suspend(reason); }
bool EspNow_resume(ConnectivitySuspendReason reason) { return NmEspNowConnection::resume(reason); }
void EspNow_onRadioState(bool ready)
{
    if (!serviceEnabled || serviceSuspended)
        return;
    if (!ready)
    {
        serviceState = ConnectivityState::STARTING;
        OnDisconnectedIngress(ConnectionType::ESP_NOW);
        serviceChanged();
        NmEspNowConnection::end();
        return;
    }
    if (!NmEspNowConnection::running())
    {
        serviceState = ConnectivityState::STARTING;
        if (!NmEspNowConnection::begin())
            serviceState = ConnectivityState::ERROR;
        else
            serviceState = ConnectivityState::CONNECTING;
        serviceChanged();
    }
}
}

#endif // NM_NETWORK_ESPNOW
