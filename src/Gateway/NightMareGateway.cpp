#include <Gateway/NightMareGateway.h>
#if NM_ENABLE_GATEWAY
#if NM_GATEWAY_MQTT
#include <Gateway/EspMqtt/espMqtt.h>
#endif
#include <Gateway/EspNow/espBroker.h>
#include <Gateway/Broker/Message.h>
#include <Core/Time.h>
#include <Gateway/GatewayStats.h>
#include <Gateway/HardwareTrace.h>
#include <Network/EspNow/NightMareEspNow/Auth.h>
#include <Gateway/GatewayState.h>
#include <Gateway/GatewayRecovery.h>
#include <Gateway/GatewayNode.h>
#include "cJSON.h"
#include "freertos/queue.h"
#include "esp_task_wdt.h"
#include <strings.h>
#include "esp_timer.h"
#include <Core/Logs.h>
#include <atomic>
#include <new>
#include <utility>

static const char *TAG = "gateway";

// Only has to absorb one tick's worth of arrivals, but subscribing to "#" makes
// the broker replay every retained topic at once, so the burst is large.
#define INBOUND_QUEUE_DEPTH 64
#define DEBUG_TOPIC "gateway/debug/in"
#define CONTROL_REQUEST_TOPIC "Control/request"

// TASK mode only; NULL in MANUAL mode, so nothing notifies the application's task.
static TaskHandle_t s_task = NULL;
static NightMareGatewayTickMode s_tickMode = NightMareGatewayTickMode::TASK;
static std::atomic<bool> s_initialized{false};
// Advanced by gatewayStep() only, so by whoever ticks the gateway.
static NightMareGatewayState s_state = NightMareGatewayState::WaitForWifiStack;
static void (*s_onLinkChange)(NightMareGatewayLink link) = nullptr;

static QueueHandle_t s_inbound = NULL; // NightMare::Message* from the MQTT task
static NightMare::MessageVault s_vault;
static bool s_mqttStarted = false;
static std::string s_lastPublishedClients;
static std::string s_lastPublishedNetwork;
static GatewayRecovery s_gatewayRecovery;
static std::atomic<NightMareGatewayState> s_gatewayState{NightMareGatewayState::Stopped};
static std::atomic<bool> s_enabled{true}; // cleared by "gateway off", see NightMareGateway_enable()

// What NightMareGateway_init() kept from its config. The PSK copy only lives
// until the gateway tick hands it to espBroker_init(), which keeps its own.
static bool s_espnowEnabled = true;
static bool s_mqttEnabled = true;
static void (*s_onTick)() = nullptr;
// Wi-Fi is the application's; these say whether it is far enough along.
static bool (*s_radioReady)() = nullptr;
static bool (*s_ipReady)() = nullptr;
static bool s_mqttTls = false;
static std::atomic<bool> s_hardwareTrace{true};
// Diagnostic switch ("espnow off"): ESP-NOW is torn down while MQTT keeps
// running, to tell a Wi-Fi/ESP-NOW coexistence problem from an MQTT/TCP one.
// The gateway task applies it; "gateway off" clears it.
static std::atomic<bool> s_espnowPaused{false};
static bool s_espnowPausedApplied = false;
static uint8_t s_pendingPsk[NightMare::EspNowAuth::MaxPskLength];
static size_t s_pendingPskLength = 0;

// Keeps a copy of anything flagged persistent so a device that subscribes later
// still gets the last value, the way a broker would replay retained messages.
static void retainIfPersistent(const NightMare::Message &message)
{
    if (!message.persistent)
        return;

    if (isHardwareTopic(message.topic))
        HW_TRACE("vault %s '%s' %u B (%s)", message.payload.empty() ? "erase" : "store", message.topic.c_str(),
                 (unsigned)message.payload.size(),
                 message.direction == NightMare::Direction::REMOTE_TO_LOCAL ? "from broker" : "from device");

    s_vault.retainMessage(message.topic, message.payload, (uint32_t)message.payload.size(), message.direction,
                          (uint64_t)(esp_timer_get_time() / 1000));
}

// Where a message came in: the ESP-NOW side (a device published it) or the
// MQTT broker. onMessage() stamps this into Message::direction on arrival.
enum class Origin : uint8_t
{
    Local,
    Remote,
};

static Origin originOf(const NightMare::Message &message)
{
    return message.direction == NightMare::Direction::REMOTE_TO_LOCAL ? Origin::Remote : Origin::Local;
}

// The two sides behind their feature flags. Everything below goes through
// these rather than the drivers, so a side compiled out is simply never up.
static bool ipReady()
{
    return s_ipReady != nullptr && s_ipReady();
}

void NightMareGateway_setHardwareTrace(bool enabled)
{
    s_hardwareTrace.store(enabled, std::memory_order_relaxed);
}

bool NightMareGateway_hardwareTrace()
{
    return s_hardwareTrace.load(std::memory_order_relaxed);
}

static bool localUp()
{
#if NM_GATEWAY_ESPNOW
    return espBroker_subscriberCount() > 0;
#else
    return false;
#endif
}

static bool remoteUp()
{
#if NM_GATEWAY_MQTT
    return mqtt_is_connected();
#else
    return false;
#endif
}

static bool sendLocal(const NightMare::Message &message, const NightMare::Device *except = nullptr)
{
#if NM_GATEWAY_ESPNOW
    // Every connected local device with a filter matching the topic.
    return espBroker_broadcastMessage(message, except);
#else
    (void)message;
    (void)except;
    return false;
#endif
}

static bool sendRemote(const NightMare::Message &message)
{
#if NM_GATEWAY_MQTT
    if (!mqtt_is_connected() || !mqtt_publish(message))
        return false;
    gwCount(g_gatewayStats.msgsToRemote);
    return true;
#else
    (void)message;
    return false;
#endif
}

static void snapshotDevices(std::vector<EspBrokerDeviceInfo> &devices)
{
#if NM_GATEWAY_ESPNOW
    espBroker_snapshotDevices(devices);
#else
    devices.clear();
#endif
}

// Only on the gateway's own task: in MANUAL mode the watchdog belongs to the
// application's task, which may not be subscribed to it.
static void feedWatchdog()
{
    if (s_task != NULL)
        esp_task_wdt_reset();
}

static NightMare::Message retainedMessage(const std::string &topic, const std::string &payload)
{
    NightMare::Message message;
    message.topic = topic;
    message.payload.assign(payload.begin(), payload.end());
    message.persistent = true;
    message.direction = NightMare::Direction::LOCAL_TO_REMOTE;
    return message;
}

#if NM_GATEWAY_MQTT
bool mqtt_prepare_session(esp_mqtt_client_handle_t client)
{
    std::vector<NightMare::RetainedEntry> retained;
    s_vault.snapshot(retained);
    for (const NightMare::RetainedEntry &entry : retained)
    {
        if (entry.message.direction == NightMare::Direction::LOCAL_TO_REMOTE &&
            !mqtt_publish_immediate(client, entry.message))
            return false;
    }

    std::vector<EspBrokerDeviceInfo> devices;
    snapshotDevices(devices);
    const NightMare::Message status = retainedMessage(GatewayState::statusTopic(), GatewayState::statusJson(true));
    const NightMare::Message network = retainedMessage(GatewayState::networkTopic(), GatewayState::networkJson(true));
    const NightMare::Message clients = retainedMessage(GatewayState::clientsTopic(), GatewayState::clientsJson(devices));
    if (!mqtt_publish_immediate(client, status) || !mqtt_publish_immediate(client, network) ||
        !mqtt_publish_immediate(client, clients))
        return false;
    s_lastPublishedNetwork.assign(network.payload.begin(), network.payload.end());
    s_lastPublishedClients.assign(clients.payload.begin(), clients.payload.end());
    return true;
}
#endif

bool publishToAll(const NightMare::Message &message)
{
    retainIfPersistent(message);
    const bool local = sendLocal(message);
    const bool remote = sendRemote(message);
    return local || remote;
}

// Same side the request came from, so a question asked on one side is not
// answered on the other. Delivery is still topic based: there is no
// unicast-to-sender path, so the asker has to be subscribed to the reply topic.
bool respondTo(const NightMare::Message &current, const NightMare::Message &response, ReplyTarget target)
{
    if (target == ReplyTarget::Both)
        return publishToAll(response);

    retainIfPersistent(response);
    return originOf(current) == Origin::Remote ? sendRemote(response) : sendLocal(response);
}

// A request on "<x>/in" is answered on "<x>/out" -- the same convention a
// NightMareNetwork device's own console uses (both the unaddressed console/in
// and the id-addressed console/controlled/<id>/in forms; the id just rides
// along inside <x>). Goes back to the side the request came from (see respondTo),
// unless `target` says Both.
void replyMessage(const NightMare::Message &current, const NightMare::Message &response, ReplyTarget target)
{
    static const std::string suffix = "/in";
    if (current.topic.size() <= suffix.size() ||
        current.topic.compare(current.topic.size() - suffix.size(), suffix.size(), suffix) != 0)
    {
        LOG_WARNING(TAG, "Can't reply to '%s', it doesn't end in /in", current.topic.c_str());
        return;
    }

    NightMare::Message reply = response;
    reply.topic = current.topic.substr(0, current.topic.size() - suffix.size()) + "/out";
    reply.direction = NightMare::Direction::LOCAL_TO_REMOTE;

    respondTo(current, reply, target);
}

// {"wifi":bool,"mqtt":bool,"subscribers":N,"devices":[{"mac","cid","state","subs","lastSeenS","lastWill"},...]}
static NightMare::Message buildDebugTelemetry()
{
    std::string json = "{\"wifi\":";
    json += ipReady() ? "true" : "false";
    json += ",\"mqtt\":";
    json += remoteUp() ? "true" : "false";
#if NM_GATEWAY_ESPNOW
    json += ",\"subscribers\":" + std::to_string(espBroker_subscriberCount());
#else
    json += ",\"subscribers\":0";
#endif
    json += ",\"devices\":[";

    const uint64_t nowMs = (uint64_t)(esp_timer_get_time() / 1000);
#if NM_GATEWAY_ESPNOW
    const uint8_t deviceCount = espBroker_deviceCount();
#else
    const uint8_t deviceCount = 0;
#endif
    for (uint8_t i = 0; i < deviceCount; i++)
    {
#if NM_GATEWAY_ESPNOW
        const NightMare::Device *device = espBroker_deviceAt(i);
        if (device == nullptr)
            continue;
        if (i > 0)
            json += ",";

        char row[160];
        snprintf(row, sizeof(row), "{\"mac\":\"%s\",\"cid\":%u,\"state\":\"%s\",\"subs\":%u,\"lastSeenS\":%llu,\"lastWill\":%s}",
                 device->address().toString().c_str(), (unsigned)device->cid(),
                 NightMare::connectionStateName(device->state()), (unsigned)device->subscriptionCount(),
                 (unsigned long long)((nowMs - device->lastSeenAtMs()) / 1000),
                 device->hasLastWill() ? "true" : "false");
        json += row;
#endif
    }
    json += "]}";

    NightMare::Message telemetry;
    telemetry.payload.assign(json.begin(), json.end());
    return telemetry;
}

// "Control/request" from a local device, payload naming what it wants. Answers
// go out on "Control/<what>" back to the asking side only; the request is then
// consumed, so a remote Control service does not answer it a second time.
// Returns false to let the request pass through (unknown request, or nothing
// valid to answer with) and be bridged to MQTT.
//   time -> Control/time {"timestamp": <unix seconds, UTC>, "offset": 0}
//           NightMareNetwork's syncTime() requires both fields; it only uses
//           the timestamp.
static bool answerControlRequest(const NightMare::Message &request)
{
    std::string what(request.payload.begin(), request.payload.end());
    while (!what.empty() && isspace((unsigned char)what.back()))
        what.pop_back();

    if (strcasecmp(what.c_str(), "time") == 0)
    {
        if (!NightMare::Time::valid())
        {
            LOG_WARNING(TAG, "Time requested but the clock is not synced yet");
            return false;
        }
        char json[48];
        snprintf(json, sizeof(json), "{\"timestamp\":%lld,\"offset\":0}", (long long)NightMare::Time::now());

        NightMare::Message reply;
        reply.topic = "Control/time";
        reply.payload.assign(json, json + strlen(json));
        reply.direction = NightMare::Direction::LOCAL_TO_REMOTE;
        respondTo(request, reply);
        return true;
    }
    return false;
}

// Messages the gateway can answer itself. A handler returns true when it
// resolved the message (it replied, or deliberately swallowed it): the message
// is then consumed and goes no further. False means "not mine / cannot answer",
// and the message escalates: routed to local subscribers and bridged to MQTT
// as usual. To resolve another request locally, add a row.
struct LocalHandler
{
    const char *filter;
    bool localOnly; // only for messages that came from an ESP-NOW device
    bool (*handle)(const NightMare::Message &message);
};

static bool answerDebugRequest(const NightMare::Message &request)
{
    replyMessage(request, buildDebugTelemetry());
    return true;
}

static const LocalHandler s_localHandlers[] = {
    {DEBUG_TOPIC, false, answerDebugRequest},
    {CONTROL_REQUEST_TOPIC, true, answerControlRequest},
};

static bool resolveLocally(const NightMare::Message &message)
{
    for (const LocalHandler &handler : s_localHandlers)
    {
        if (handler.localOnly && originOf(message) != Origin::Local)
            continue;
        if (topicMatchesPattern(message.topic, handler.filter) && handler.handle(message))
            return true;
    }
    return false;
}

// The one place a message from either side is handled, before it is routed:
// device == nullptr means it arrived over MQTT, a non-null device means it
// arrived from that local ESP-NOW peer over ESP-NOW. Order: try to resolve it
// locally, and only if the gateway cannot, route and bridge it.
void onMessage(const NightMare::Device *device, NightMare::Message message)
{
    gwCount(device != nullptr ? g_gatewayStats.msgsFromLocal : g_gatewayStats.msgsFromRemote);
    message.direction = device != nullptr ? NightMare::Direction::LOCAL_TO_REMOTE : NightMare::Direction::REMOTE_TO_LOCAL;

    if (device == nullptr && message.persistent)
    {
        std::vector<NightMare::Message> recovered;
        if (s_gatewayRecovery.consume(message, recovered))
        {
            for (NightMare::Message &will : recovered)
                onMessage(nullptr, std::move(will));
            // Client projections are recovery-internal. Gateway presence must
            // still reach local ESP-NOW subscribers so their retained
            // candidate can be invalidated by the generation-matched LWT.
            if (topicMatchesPattern(message.topic, "+/gateway/clients"))
                return;
        }
    }

    // A retained broker status is older than a live authenticated ESP-NOW
    // session. Never let an offline replay overwrite the gateway-owned truth.
    if (device == nullptr && message.persistent && topicMatchesPattern(message.topic, "+/status"))
    {
#if NM_GATEWAY_ESPNOW
        const std::string name = message.topic.substr(0, message.topic.size() - strlen("/status"));
        if (espBroker_connectedDeviceNamed(name))
            return;
#endif
    }

    // Addressed to the gateway itself as a NightMare device: its Resources and
    // its console.
    if (GatewayNode::handleMessage(message))
        return;

    if (resolveLocally(message))
        return;

    retainIfPersistent(message);

    // Broker stage: every local device subscribed to this topic, minus
    // whoever produced it, which is what stops a message bouncing back to
    // its own sender. A remote (MQTT) message has already reached its far
    // side by arriving here at all.
    sendLocal(message, device);

    if (device != nullptr)
        sendRemote(message);
}

#if NM_GATEWAY_ESPNOW
void espBroker_onMessage(NightMare::Device *device, NightMare::Message message)
{
    if (isHardwareTopic(message.topic))
        HW_TRACE("rx espnow from %s: '%s' %u B retained=%d", device->address().toString().c_str(),
                 message.topic.c_str(), (unsigned)message.payload.size(), (int)message.persistent);

    if (topicMatchesPattern(message.topic, "+/status"))
    {
        std::string name = message.topic.substr(0, message.topic.size() - 7);
        // Do something with the status update, e.g., update a display or trigger an action.
        if (device)
        {
            device->setAssumedName(name);
        }
    }
    onMessage(device, std::move(message));
}

// A device just subscribed, so replay whatever we have retained below its filter.
void espBroker_onSubscribe(const NightMare::Device *device, const std::string &filter)
{
    std::vector<NightMare::Message> retained;
    if (!s_vault.getMessagesForTopic(filter, retained))
        return;

    for (const NightMare::Message &message : retained)
        espBroker_sendMessage(device, message);
}
#endif

#if NM_GATEWAY_MQTT
// Runs in the esp-mqtt task, so hand the message to the gateway tick instead of
// touching the device table from here.
void mqtt_onMessage(NightMare::Message message)
{
    if (s_inbound == NULL)
        return;
    NightMare::Message *copy = new (std::nothrow) NightMare::Message(std::move(message));
    if (copy == nullptr)
    {
        LOG_WARNING(TAG, "Unable to allocate inbound message envelope");
        return;
    }
    if (xQueueSend(s_inbound, &copy, 0) != pdTRUE)
    {
        LOG_WARNING(TAG, "Inbound queue full, dropping '%s'", message.topic.c_str());
        delete copy;
        return;
    }
    NightMareGateway_wake(); // drain now, not at the next tick
}
#endif

static void drainInbound(void)
{
    NightMare::Message *message = NULL;
    while (xQueueReceive(s_inbound, &message, 0) == pdTRUE)
    {
        onMessage(nullptr, std::move(*message));
        delete message;
        // A reconnect replays every retained topic at once, and each one can
        // take several bounded ESP-NOW sends; don't let the burst look like a hang.
        feedWatchdog();
    }
}

static void drainInboundDiscarding(void)
{
    NightMare::Message *message = NULL;
    while (xQueueReceive(s_inbound, &message, 0) == pdTRUE)
        delete message;
}

// Everything the gateway runs is stopped, so the radio and the broker see
// nothing from it. The vault is kept: it is the gateway's memory, not a session.
static void stopServices(void)
{
    LOG_WARNING(TAG, "Gateway stopping");
    s_espnowPaused.store(false);
    s_espnowPausedApplied = false;
#if NM_GATEWAY_ESPNOW
    espBroker_shutdown();
#endif
#if NM_GATEWAY_MQTT
    if (s_mqttStarted)
    {
        mqtt_stop();
        s_mqttStarted = false;
    }
#endif
    drainInboundDiscarding();
}

static void reportLink(void)
{
    if (s_onLinkChange == nullptr)
        return;
    const bool remote = remoteUp();
    const bool local = localUp();
    const NightMareGatewayLink link = remote && local ? NightMareGatewayLink::BRIDGED
                                      : remote        ? NightMareGatewayLink::REMOTE
                                      : local         ? NightMareGatewayLink::LOCAL
                                                      : NightMareGatewayLink::NONE;

    static bool reported = false;
    static NightMareGatewayLink current = NightMareGatewayLink::NONE;
    if (reported && link == current)
        return;
    current = link;
    reported = true;
    s_onLinkChange(link);
}

#if NM_GATEWAY_MQTT
static void publishGatewayProjection()
{
    if (!mqtt_is_connected())
        return;
    std::vector<EspBrokerDeviceInfo> devices;
    snapshotDevices(devices);
    const std::string clients = GatewayState::clientsJson(devices);
    if (clients != s_lastPublishedClients && mqtt_publish(retainedMessage(GatewayState::clientsTopic(), clients)))
        s_lastPublishedClients = clients;
    const std::string network = GatewayState::networkJson(true);
    if (network != s_lastPublishedNetwork && mqtt_publish(retainedMessage(GatewayState::networkTopic(), network)))
        s_lastPublishedNetwork = network;
}

// TLS verifies the broker certificate's validity dates, so connecting before
// SNTP has set the clock fails (-0x2700, cert not yet valid).
static bool mqttClockReady()
{
    return !s_mqttTls || NightMare::Time::valid();
}
#endif // NM_GATEWAY_MQTT

#if NM_GATEWAY_ESPNOW
static void startEspNow()
{
    // The first time the key comes from the config; after a "gateway off"/"on"
    // the broker still holds its own copy.
    const bool firstStart = s_pendingPskLength != 0;
    const bool up = firstStart ? espBroker_init(s_pendingPsk, s_pendingPskLength) : espBroker_resume();
    if (!up)
        LOG_ERROR(TAG, "ESP-NOW unavailable%s", s_mqttEnabled ? ", running MQTT only" : "");
    NightMare::EspNowAuth::wipe(s_pendingPsk, sizeof(s_pendingPsk));
    s_pendingPskLength = 0;
}

// The "espnow on|off" diagnostic, then the ESP-NOW side's own work.
static void serviceEspNow()
{
    if (s_espnowPaused.load() != s_espnowPausedApplied)
    {
        if (s_espnowPaused.load())
        {
            espBroker_shutdown();
            LOG_WARNING(TAG, "ESP-NOW paused (diagnostic): devices are dropped, MQTT keeps running");
            s_espnowPausedApplied = true;
        }
        else if (espBroker_resume())
        {
            LOG_WARNING(TAG, "ESP-NOW resumed: devices have to reconnect");
            s_espnowPausedApplied = false;
        }
        else
        {
            s_espnowPaused.store(true); // could not come back; stay paused, say so
            LOG_ERROR(TAG, "ESP-NOW resume failed");
        }
    }
    if (!s_espnowPausedApplied)
        espBroker_process();
}
#endif

// One pass of everything the gateway does, on whichever task ticks it.
static void gatewayStep()
{
    const bool wanted = s_enabled.load(std::memory_order_acquire);
    if (!wanted && s_state != NightMareGatewayState::Stopped)
    {
        stopServices();
        s_state = NightMareGatewayState::Stopped;
    }
    else if (wanted && s_state == NightMareGatewayState::Stopped)
    {
        LOG_DEBUG(TAG, "Gateway starting");
        s_state = NightMareGatewayState::WaitForWifiStack;
    }

    switch (s_state)
    {
    case NightMareGatewayState::WaitForWifiStack:
        // ESP-NOW only needs the radio running, so it comes up before (and
        // independently of) any association with an access point.
        if (s_radioReady())
            s_state = s_espnowEnabled ? NightMareGatewayState::StartEspNow : NightMareGatewayState::Running;
        break;

    case NightMareGatewayState::StartEspNow:
#if NM_GATEWAY_ESPNOW
        startEspNow();
#endif
        s_state = NightMareGatewayState::Running;
        break;

    case NightMareGatewayState::Running:
#if NM_GATEWAY_MQTT
        if (s_mqttEnabled && !s_mqttStarted && ipReady() && mqttClockReady())
        {
            LOG_DEBUG(TAG, "Network up, starting MQTT");
            mqtt_init();
            s_mqttStarted = true;
        }
#endif
#if NM_GATEWAY_ESPNOW
        if (s_espnowEnabled)
            serviceEspNow();
#endif
        drainInbound();
#if NM_GATEWAY_MQTT
        publishGatewayProjection();
#endif
        break;

    case NightMareGatewayState::Stopped:
        break;
    }

    s_gatewayState.store(s_state, std::memory_order_release);

    if (s_onTick != nullptr)
        s_onTick();
    GatewayNode::tick();
    reportLink();
}

static void gatewayTask(void *)
{
    esp_task_wdt_add(NULL);
    GatewayNode::begin();
    while (1)
    {
        gatewayStep();
        esp_task_wdt_reset();
        // Sleeps until work arrives (an ESP-NOW packet or an MQTT message wakes
        // this task), or NM_GATEWAY_TICK_MS at most for the timed work: beacon,
        // pruning, link status.
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(NM_GATEWAY_TICK_MS));
    }
}

void NightMareGateway_wake()
{
    if (s_task != NULL)
        xTaskNotifyGive(s_task);
}

void NightMareGateway_tick()
{
    if (!s_initialized.load(std::memory_order_acquire) || s_tickMode != NightMareGatewayTickMode::MANUAL)
        return;
    gatewayStep();
}

size_t NightMareGateway_vaultSnapshot(std::vector<NightMare::RetainedEntry> &out, uint32_t *version)
{
    return s_vault.snapshot(out, version);
}

uint32_t NightMareGateway_vaultVersion()
{
    return s_vault.version();
}

size_t NightMareGateway_vaultSize()
{
    return s_vault.size();
}

void NightMareGateway_enable(bool enabled)
{
    s_enabled.store(enabled, std::memory_order_release);
    NightMareGateway_wake(); // act now, not at the next timed tick
}

void NightMareGateway_pauseEspNow(bool paused)
{
    s_espnowPaused.store(paused);
    NightMareGateway_wake();
}

bool NightMareGateway_espNowPaused()
{
    return s_espnowPaused.load();
}

bool NightMareGateway_enabled()
{
    return s_enabled.load(std::memory_order_acquire);
}

NightMareGatewayState NightMareGateway_state()
{
    return s_gatewayState.load(std::memory_order_acquire);
}

const char *NightMareGateway_stateName(NightMareGatewayState state)
{
    switch (state)
    {
    case NightMareGatewayState::Stopped:
        return "stopped";
    case NightMareGatewayState::WaitForWifiStack:
        return "waiting_for_wifi_stack";
    case NightMareGatewayState::StartEspNow:
        return "starting_esp_now";
    case NightMareGatewayState::Running:
        return "running";
    }
    return "unknown";
}

bool NightMareGateway_init(const NightMareGatewayConfig &config)
{
    if (s_initialized.load(std::memory_order_acquire))
    {
        LOG_ERROR(TAG, "Already initialized");
        return false;
    }
    if (config.espnowBroker && !NM_GATEWAY_ESPNOW)
    {
        LOG_ERROR(TAG, "espnowBroker needs NM_GATEWAY_ESPNOW compiled in");
        return false;
    }
    if (config.mqttClient && !NM_GATEWAY_MQTT)
    {
        LOG_ERROR(TAG, "mqttClient needs NM_GATEWAY_MQTT compiled in");
        return false;
    }
    if (config.radioReady == nullptr)
    {
        LOG_ERROR(TAG, "radioReady is required: the gateway waits for the Wi-Fi driver");
        return false;
    }
    if (config.mqttClient && (config.ipReady == nullptr || config.mqttConfig.host == nullptr ||
                              config.mqttConfig.host[0] == '\0'))
    {
        LOG_ERROR(TAG, "mqttClient needs mqttConfig.host and ipReady");
        return false;
    }

    GatewayState::begin();
    GatewayState::setUplink(config.mqttClient, config.mqttConfig.uplink == NightMareGatewayUplink::REMOTE);
    // mqttBroker is not implemented; the flag is ignored.
    s_espnowEnabled = config.espnowBroker;
    s_mqttEnabled = config.mqttClient;
    s_onTick = config.onTick;
    s_onLinkChange = config.onLinkChange;
    s_tickMode = config.tickMode;
    s_radioReady = config.radioReady;
    s_ipReady = config.ipReady;
#if NM_GATEWAY_MQTT
    if (s_mqttEnabled)
    {
        s_mqttTls = config.mqttConfig.tls;
        mqtt_configure(config.mqttConfig);
    }
#endif
    if (s_espnowEnabled)
    {
        const NightMareEspNowConfig &espnow = config.espnowConfig;
        if (espnow.psk == NULL || espnow.pskLength < NightMare::EspNowAuth::MinPskLength ||
            espnow.pskLength > NightMare::EspNowAuth::MaxPskLength)
        {
            LOG_ERROR(TAG, "ESP-NOW needs a %u..%u byte network key, got %u",
                     (unsigned)NightMare::EspNowAuth::MinPskLength,
                     (unsigned)NightMare::EspNowAuth::MaxPskLength, (unsigned)espnow.pskLength);
            return false;
        }
        memcpy(s_pendingPsk, espnow.psk, espnow.pskLength);
        s_pendingPskLength = espnow.pskLength;
    }

    if (s_inbound == NULL)
        s_inbound = xQueueCreate(INBOUND_QUEUE_DEPTH, sizeof(NightMare::Message *));
    if (s_inbound == NULL)
    {
        LOG_ERROR(TAG, "Failed to allocate inbound queue");
        return false;
    }

    if (s_tickMode == NightMareGatewayTickMode::MANUAL)
    {
        // Resources are bound and announced on the caller: the task that goes
        // on to tick them.
        GatewayNode::begin();
        s_initialized.store(true, std::memory_order_release);
        return true;
    }

    s_initialized.store(true, std::memory_order_release);
    if (xTaskCreate(gatewayTask, "GatewayTask", NM_GATEWAY_TASK_STACK, NULL, NM_GATEWAY_TASK_PRIORITY, &s_task) != pdPASS)
    {
        LOG_ERROR(TAG, "Failed to start the gateway task");
        s_task = NULL;
        s_initialized.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

#endif // NM_ENABLE_GATEWAY
