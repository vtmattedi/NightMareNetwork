#include <Gateway/NightMareGateway.h>
#if NM_GATEWAY_MQTT
#include <Gateway/EspMqtt/espMqtt.h>
#include "mqtt_client.h"
#include "mqtt5_client.h"
#include <Core/Logs.h>
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include <atomic>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <string>
#include <Gateway/GatewayState.h>
#include <Gateway/GatewayStats.h>
#include <Gateway/HardwareTrace.h>
#include "esp_timer.h"
#include "lwip/opt.h"
#include "lwip/stats.h"
#include "lwip/tcp.h"
#include "lwip/tcpip.h"
#include "lwip/priv/tcp_priv.h"
#include <new>
#include <utility>

static const char *TAG = "espMqtt";
static esp_mqtt_client_handle_t s_client = NULL;
static std::atomic<bool> s_connected{false};

// mqtt_configure()'s copy: the client points into these strings while it lives.
struct MqttSettings
{
    std::string host;
    uint16_t port = 0;
    bool tls = false;
    std::string caCertificate;
    std::string username;
    std::string password;
};
static MqttSettings s_settings;

static std::string text(const char *value)
{
    return value != nullptr ? std::string(value) : std::string();
}

void mqtt_configure(const NightMareMqttConfig &config)
{
    s_settings.host = text(config.host);
    s_settings.port = config.port;
    s_settings.tls = config.tls;
    s_settings.caCertificate = text(config.caCertificate);
    s_settings.username = text(config.username);
    s_settings.password = text(config.password);
}

// esp_mqtt_client_publish() takes the client's API lock, which the esp-mqtt
// task holds through a stalled TLS read/write, DNS lookup or reconnect -- for
// seconds. Called from the gateway task that blocked it past the task
// watchdog, so publishes are handed to this queue and a task of their own
// does the blocking call instead.
#define OUTBOUND_QUEUE_DEPTH 32
#define PUBLISHER_STACK_SIZE 6144
#define PUBLISHER_PRIORITY 4
static QueueHandle_t s_outbound = NULL; // NightMare::Message*; NULL is the "session connected" marker
static std::atomic<uint32_t> s_dropped{0};

// MQTT 5 brokers cap unacknowledged QoS1 publishes (Receive Maximum; 10 on the
// broker in use) and esp-mqtt refuses to send past it. PUBACKs are read by the
// esp-mqtt task, so nothing may publish from its event handler in bulk: the
// acks that would free a slot can't be processed until the handler returns.
// Publishing therefore stays on the publisher task, paced to this window.
#define MAX_INFLIGHT_PUBLISHES 8
#define INFLIGHT_WAIT_MS 5000
// pdMS_TO_TICKS(5) is 0 at CONFIG_FREERTOS_HZ=100, and vTaskDelay(0) only
// yields: a wait loop built on it never blocks, so IDLE0 starves and the task
// watchdog fires. Poll in whole ticks, at least one.
#define POLL_TICKS (pdMS_TO_TICKS(5) > 0 ? pdMS_TO_TICKS(5) : 1)
static std::atomic<int> s_inflight{0};
static std::atomic<bool> s_sessionPending{false};
static std::atomic<uint32_t> s_session{0}; // bumped on every CONNECTED

// --- Link diagnostics ----------------------------------------------------------
// Read on every disconnect to tell a link that died suddenly from one that got
// slower first, and to tie it to what else the radio was doing. Written from the
// esp-mqtt task (events) and the publisher task (sends); all relaxed atomics.
static inline int64_t nowUs(void) { return esp_timer_get_time(); }

// Publish time per msg_id, so a PUBACK yields a latency and a disconnect can
// report how long the oldest unanswered publish has waited. A slot a PUBACK
// beat us to is simply never matched; they are all cleared on CONNECTED.
#define ACK_SLOTS 16
struct AckSlot
{
    std::atomic<int> id{0};
    std::atomic<int64_t> sentUs{0};
    std::atomic<bool> watched{false}; // a <device>/hardware publish: its PUBACK is logged
};
static AckSlot s_ackSlots[ACK_SLOTS];
static std::atomic<int64_t> s_connectedUs{0};
static std::atomic<int64_t> s_lastRxUs{0};      // anything from the broker: DATA, PUBACK, SUBACK
static std::atomic<int64_t> s_lastAckUs{0};     // last PUBACK
static std::atomic<int64_t> s_lastPublishUs{0}; // last publish handed to esp-mqtt
static std::atomic<uint32_t> s_published{0};    // since CONNECTED
static std::atomic<uint32_t> s_acked{0};
static std::atomic<uint32_t> s_ackLatSumMs{0};
static std::atomic<uint32_t> s_ackLatMaxMs{0};
static std::atomic<uint32_t> s_paceWaitMaxMs{0}; // longest publish_paced() stall on a full window
// msg ids of the last PUBACKs (newest overwrites oldest); mqtttest waits on it.
static std::atomic<int> s_recentAcked[16];
static std::atomic<uint32_t> s_recentAckedNext{0};

// Counters read at CONNECTED, so a disconnect reports what happened during the session.
struct SessionBaseline
{
    uint32_t espnowRx, espnowTx, espnowTxFailed, toRemote, fromRemote;
#if LWIP_STATS
    uint32_t tcpXmit, tcpRecv, tcpDrop, tcpMemErr, tcpErr, linkDrop, linkMemErr;
#endif
};
static SessionBaseline s_baseline = {};
static std::atomic<bool> s_wifiDumpOnDisconnect{false};

static SessionBaseline readCounters(void)
{
    SessionBaseline now = {};
    now.espnowRx = g_gatewayStats.espnowRxPackets.load(std::memory_order_relaxed);
    now.espnowTx = g_gatewayStats.espnowTxPackets.load(std::memory_order_relaxed);
    now.espnowTxFailed = g_gatewayStats.espnowTxFailed.load(std::memory_order_relaxed);
    now.toRemote = g_gatewayStats.msgsToRemote.load(std::memory_order_relaxed);
    now.fromRemote = g_gatewayStats.msgsFromRemote.load(std::memory_order_relaxed);
#if LWIP_STATS
    now.tcpXmit = lwip_stats.tcp.xmit;
    now.tcpRecv = lwip_stats.tcp.recv;
    now.tcpDrop = lwip_stats.tcp.drop;
    now.tcpMemErr = lwip_stats.tcp.memerr;
    now.tcpErr = lwip_stats.tcp.err;
    now.linkDrop = lwip_stats.link.drop;
    now.linkMemErr = lwip_stats.link.memerr;
#endif
    return now;
}

// Milliseconds since `thenUs`, or -1 if it never happened this session.
static long msSince(int64_t thenUs)
{
    return thenUs == 0 ? -1L : (long)((nowUs() - thenUs) / 1000);
}

static void resetSessionDiagnostics(void)
{
    for (AckSlot &slot : s_ackSlots)
        slot.id.store(0, std::memory_order_relaxed);
    for (std::atomic<int> &id : s_recentAcked)
        id.store(0, std::memory_order_relaxed); // ids restart with the session
    const int64_t now = nowUs();
    s_connectedUs.store(now);
    s_lastRxUs.store(now);
    s_lastAckUs.store(0);
    s_lastPublishUs.store(0);
    s_published.store(0);
    s_acked.store(0);
    s_ackLatSumMs.store(0);
    s_ackLatMaxMs.store(0);
    s_paceWaitMaxMs.store(0);
    s_baseline = readCounters();
}

static void noteAck(int msgId)
{
    s_recentAcked[s_recentAckedNext.fetch_add(1) % 16].store(msgId, std::memory_order_release);
    const int64_t now = nowUs();
    s_lastRxUs.store(now);
    s_lastAckUs.store(now);
    s_acked.fetch_add(1);
    AckSlot &slot = s_ackSlots[(unsigned)msgId % ACK_SLOTS];
    if (slot.id.load(std::memory_order_acquire) != msgId)
        return;
    const uint32_t latencyMs = (uint32_t)((now - slot.sentUs.load(std::memory_order_relaxed)) / 1000);
    if (slot.watched.load(std::memory_order_relaxed))
        HW_TRACE("mqtt PUBACK for hardware publish id=%d after %ums", msgId, (unsigned)latencyMs);
    slot.id.store(0, std::memory_order_release);
    s_ackLatSumMs.fetch_add(latencyMs);
    uint32_t max = s_ackLatMaxMs.load();
    while (latencyMs > max && !s_ackLatMaxMs.compare_exchange_weak(max, latencyMs))
    {
    }
}

static void noteSent(int msgId, bool watched = false)
{
    const int64_t now = nowUs();
    s_lastPublishUs.store(now);
    s_published.fetch_add(1);
    AckSlot &slot = s_ackSlots[(unsigned)msgId % ACK_SLOTS];
    slot.sentUs.store(now, std::memory_order_relaxed);
    slot.watched.store(watched, std::memory_order_relaxed);
    slot.id.store(msgId, std::memory_order_release);
}

// --- lwIP view of the broker connection ----------------------------------------
// Taken while the link is stalled, from the tcpip thread (the only safe place to
// walk lwIP's PCB lists here: core locking is off). It answers where a stall
// lives without a packet capture:
//   unacked > 0, nrtx climbing  -> we sent, the broker's ACKs aren't arriving
//   ooseq > 0                   -> the broker's data arrived behind a lost segment
//   snd_wnd == 0                -> the broker's window is closed
//   unsent > 0, unacked == 0    -> our own stack is not getting data out
static uint16_t s_brokerPort = 0;
static SemaphoreHandle_t s_tcpSnapDone = NULL;

struct TcpSnapshot
{
    bool found;
    uint8_t state;
    uint16_t mss;
    uint32_t sndWnd, cwnd, ssthresh, rcvWnd;
    uint16_t sndBuf, queuelen;
    int rtoMs;
    int rtime;
    uint8_t nrtx;
    unsigned unackedSegs, unackedBytes, unsentSegs, unsentBytes, ooseqSegs;
    bool refused;
};
static TcpSnapshot s_tcpSnap;

static void tcp_snapshot_cb(void *)
{
    s_tcpSnap = TcpSnapshot{};
    for (struct tcp_pcb *pcb = tcp_active_pcbs; pcb != NULL; pcb = pcb->next)
    {
        if (pcb->remote_port != s_brokerPort)
            continue;
        s_tcpSnap.found = true;
        s_tcpSnap.state = (uint8_t)pcb->state;
        s_tcpSnap.mss = pcb->mss;
        s_tcpSnap.sndWnd = pcb->snd_wnd;
        s_tcpSnap.cwnd = pcb->cwnd;
        s_tcpSnap.ssthresh = pcb->ssthresh;
        s_tcpSnap.rcvWnd = pcb->rcv_wnd;
        s_tcpSnap.sndBuf = pcb->snd_buf;
        s_tcpSnap.queuelen = pcb->snd_queuelen;
        s_tcpSnap.rtoMs = (int)pcb->rto * TCP_SLOW_INTERVAL;
        s_tcpSnap.rtime = pcb->rtime;
        s_tcpSnap.nrtx = pcb->nrtx;
        for (struct tcp_seg *seg = pcb->unacked; seg != NULL; seg = seg->next)
        {
            s_tcpSnap.unackedSegs++;
            s_tcpSnap.unackedBytes += seg->len;
        }
        for (struct tcp_seg *seg = pcb->unsent; seg != NULL; seg = seg->next)
        {
            s_tcpSnap.unsentSegs++;
            s_tcpSnap.unsentBytes += seg->len;
        }
#if TCP_QUEUE_OOSEQ
        for (struct tcp_seg *seg = pcb->ooseq; seg != NULL; seg = seg->next)
            s_tcpSnap.ooseqSegs++;
#endif
        s_tcpSnap.refused = pcb->refused_data != NULL;
        break;
    }
    xSemaphoreGive(s_tcpSnapDone);
}

static void log_tcp_state(const char *why)
{
    if (s_tcpSnapDone == NULL || s_brokerPort == 0)
        return;
    xSemaphoreTake(s_tcpSnapDone, 0);
    if (tcpip_callback(tcp_snapshot_cb, NULL) != ERR_OK)
    {
        LOG_WARNING(TAG, "%s: tcp snapshot: could not queue it on the tcpip thread", why);
        return;
    }
    if (xSemaphoreTake(s_tcpSnapDone, pdMS_TO_TICKS(500)) != pdTRUE)
    {
        LOG_WARNING(TAG, "%s: tcp snapshot: tcpip thread did not answer within 500ms", why);
        return;
    }
    const TcpSnapshot &t = s_tcpSnap;
    if (!t.found)
    {
        LOG_WARNING(TAG, "%s: tcp: no connection to port %u", why, (unsigned)s_brokerPort);
        return;
    }
    LOG_WARNING(TAG, "%s: tcp state=%u mss=%u snd_wnd=%u cwnd=%u ssthresh=%u rcv_wnd=%u | unacked=%useg/%uB "
                   "unsent=%useg/%uB nrtx=%u rto=%dms rtime=%d | ooseq=%useg refused=%d | snd_buf=%u queuelen=%u",
              why, (unsigned)t.state, (unsigned)t.mss, (unsigned)t.sndWnd, (unsigned)t.cwnd, (unsigned)t.ssthresh,
              (unsigned)t.rcvWnd, t.unackedSegs, t.unackedBytes, t.unsentSegs, t.unsentBytes, (unsigned)t.nrtx,
              t.rtoMs, t.rtime, t.ooseqSegs, (int)t.refused, (unsigned)t.sndBuf, (unsigned)t.queuelen);
}

void mqtt_setWifiDumpOnDisconnect(bool enabled) { s_wifiDumpOnDisconnect.store(enabled); }
bool mqtt_wifiDumpOnDisconnect(void) { return s_wifiDumpOnDisconnect.load(); }

// Reassembly of a multi-event payload. Only ever touched by the esp-mqtt task.
static NightMare::Message s_partial;
static bool s_partialActive = false;

static void subscribeToEverything(esp_mqtt_client_handle_t client)
{
    // no_local stops the broker from echoing back what this gateway published,
    // which is what would otherwise loop MQTT -> ESP-NOW -> MQTT forever.
    esp_mqtt5_subscribe_property_config_t property = {};
    property.no_local_flag = true;
    property.retain_as_published_flag = true;

    const esp_err_t err = esp_mqtt5_client_set_subscribe_property(client, &property);
    if (err != ESP_OK)
    {
        LOG_ERROR(TAG, "Failed to set subscribe property: %s", esp_err_to_name(err));
        return;
    }

    esp_mqtt_client_subscribe(client, "#", 1);
}

// Waits for a free slot in the broker's receive window, then publishes. False
// when the connection went away, no ack freed a slot in time, or the publish
// itself was refused. Never call from the esp-mqtt task (it would wait on itself).
// The message id, or -1 on any of the failures above.
static int publish_paced_id(esp_mqtt_client_handle_t client, const NightMare::Message &message)
{
    const TickType_t started = xTaskGetTickCount();
    bool tcpLogged = false;
    while (s_inflight.load(std::memory_order_acquire) >= MAX_INFLIGHT_PUBLISHES)
    {
        const TickType_t waited = xTaskGetTickCount() - started;
        if (!tcpLogged && waited >= pdMS_TO_TICKS(2000))
        {
            tcpLogged = true; // once per stall: where the acks went
            log_tcp_state("publish window full for 2s");
        }
        if (!s_connected.load(std::memory_order_acquire) || waited >= pdMS_TO_TICKS(INFLIGHT_WAIT_MS))
            return -1;
        vTaskDelay(POLL_TICKS);
    }
    const uint32_t waitedMs = (uint32_t)((xTaskGetTickCount() - started) * portTICK_PERIOD_MS);
    if (waitedMs > s_paceWaitMaxMs.load())
        s_paceWaitMaxMs.store(waitedMs);

    s_inflight.fetch_add(1); // before publishing: the ack can beat us back here
    const int msgId = esp_mqtt_client_publish(client, message.topic.c_str(),
                                              reinterpret_cast<const char *>(message.payload.data()),
                                              (int)message.payload.size(), 1, message.persistent);
    if (msgId < 0)
    {
        s_inflight.fetch_sub(1);
        return -1;
    }
    noteSent(msgId, isHardwareTopic(message.topic));
    return msgId;
}

static bool publish_paced(esp_mqtt_client_handle_t client, const NightMare::Message &message)
{
    return publish_paced_id(client, message) >= 0;
}

// --- mqtttest: ack latency by payload size ---------------------------------------
// Publishes `count` QoS1 messages of `bytes` to <gateway id>/diag and waits for
// each PUBACK before sending the next, so every latency is one message on an
// otherwise quiet window. Run it at 200 B, 1 KB, 4 KB and 16 KB: if latency or
// failures jump with size, it is the size; if they don't, it isn't.
// The downlink twin is `mosquitto_pub -f <file>` from a PC with the gateway
// subscribed to '#'.
#define DIAG_ACK_WAIT_MS 30000
static std::atomic<bool> s_diagRunning{false};

static bool recentlyAcked(int msgId)
{
    for (const std::atomic<int> &id : s_recentAcked)
        if (id.load(std::memory_order_acquire) == msgId)
            return true;
    return false;
}

struct DiagJob
{
    uint32_t bytes;
    uint32_t count;
};

static void diag_task(void *arg)
{
    const DiagJob job = *static_cast<DiagJob *>(arg);
    delete static_cast<DiagJob *>(arg);

    NightMare::Message message;
    message.topic = GatewayState::id() + "/diag";
    message.payload.assign(job.bytes, 'x');
    message.persistent = false;

    uint32_t acked = 0, failed = 0, timedOut = 0;
    uint32_t minMs = UINT32_MAX, maxMs = 0;
    uint64_t sumMs = 0;
    LOG("mqtttest", "mqtttest: %u x %u bytes to '%s'", (unsigned)job.count, (unsigned)job.bytes,
             message.topic.c_str());

    for (uint32_t i = 0; i < job.count && s_connected.load(std::memory_order_acquire); i++)
    {
        const int64_t sent = nowUs();
        const int id = publish_paced_id(s_client, message);
        if (id < 0)
        {
            failed++;
            LOG("mqtttest", "  #%u: publish refused or window never freed", (unsigned)(i + 1));
            continue;
        }
        bool ok = false;
        while ((nowUs() - sent) / 1000 < DIAG_ACK_WAIT_MS && s_connected.load(std::memory_order_acquire))
        {
            if (recentlyAcked(id))
            {
                ok = true;
                break;
            }
            vTaskDelay(POLL_TICKS);
        }
        const uint32_t ms = (uint32_t)((nowUs() - sent) / 1000);
        if (!ok)
        {
            timedOut++;
            LOG("mqtttest", "  #%u: no PUBACK after %ums", (unsigned)(i + 1), (unsigned)ms);
            continue;
        }
        acked++;
        sumMs += ms;
        minMs = ms < minMs ? ms : minMs;
        maxMs = ms > maxMs ? ms : maxMs;
        LOG("mqtttest", "  #%u: PUBACK in %ums", (unsigned)(i + 1), (unsigned)ms);
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    if (acked != 0)
        LOG("mqtttest", "mqtttest done: %u bytes: acked %u/%u, no ack %u, refused %u, latency min/avg/max=%u/%u/%ums",
                 (unsigned)job.bytes, (unsigned)acked, (unsigned)job.count, (unsigned)timedOut, (unsigned)failed,
                 (unsigned)minMs, (unsigned)(sumMs / acked), (unsigned)maxMs);
    else
        LOG_WARNING("mqtttest", "mqtttest done: %u bytes: nothing acked (no ack %u, refused %u)", (unsigned)job.bytes,
                 (unsigned)timedOut, (unsigned)failed);
    s_diagRunning.store(false);
    vTaskDelete(NULL);
}

bool mqtt_startDiagTest(uint32_t bytes, uint32_t count)
{
    if (s_client == NULL || !s_connected.load(std::memory_order_acquire) || bytes == 0 || count == 0)
        return false;
    bool expected = false;
    if (!s_diagRunning.compare_exchange_strong(expected, true))
        return false;
    DiagJob *job = new (std::nothrow) DiagJob{bytes, count};
    if (job == nullptr || xTaskCreate(diag_task, "MqttDiag", 4096, job, 3, NULL) != pdPASS)
    {
        delete job;
        s_diagRunning.store(false);
        return false;
    }
    return true;
}

// Publishes the gateway's retained truth, and only then subscribes so the
// broker's retained replay can't overwrite it. On failure the connection is
// dropped: a half-prepared session would look up while nothing is subscribed.
static void run_session_setup(void)
{
    const uint32_t session = s_session.load();
    const bool prepared = mqtt_prepare_session(s_client);
    if (session != s_session.load() || !s_connected.load(std::memory_order_acquire))
        return; // the connection went away meanwhile; the next CONNECTED starts over

    if (prepared)
    {
        subscribeToEverything(s_client);
        return;
    }
    LOG_ERROR(TAG, "Gateway state publication failed; reconnecting");
    esp_mqtt_client_disconnect(s_client);
}

// One line to tell a starved radio/TCP stack from a bad link when MQTT drops:
// internal heap (what lwIP, Wi-Fi and TLS actually use, unlike the PSRAM-padded
// totals), and the signal to the AP.
static void log_link_health(const char *why)
{
    wifi_ap_record_t ap = {};
    const bool haveAp = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    LOG_WARNING(TAG, "%s: internal heap free=%u min=%u largest=%u, rssi=%d ch=%u, inflight=%d",
             why, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             haveAp ? ap.rssi : 0, haveAp ? (unsigned)ap.primary : 0u, s_inflight.load());
}

// The second half of a disconnect report: what the session looked like from the
// inside. The idle times say how long the link had been silent before esp-mqtt
// noticed (keepalive 60s means ~60s is a link that went quiet right after the
// last good exchange); the ack latency shows a link that slowed before it died;
// the ESP-NOW and lwIP deltas show what else shared the radio and the stack.
static void log_session_summary(void)
{
    const SessionBaseline now = readCounters();
    const uint32_t acked = s_acked.load();
    const int64_t nowUsValue = nowUs();

    long oldestUnackedMs = -1;
    for (AckSlot &slot : s_ackSlots)
    {
        if (slot.id.load(std::memory_order_acquire) == 0)
            continue;
        const long age = (long)((nowUsValue - slot.sentUs.load(std::memory_order_relaxed)) / 1000);
        if (age > oldestUnackedMs)
            oldestUnackedMs = age;
    }

    LOG_WARNING(TAG, "session: up=%lds idle(rx=%ldms ack=%ldms tx=%ldms) published=%u acked=%u "
                   "ack_latency(avg=%ums max=%ums) oldest_unacked=%ldms pace_wait_max=%ums",
              msSince(s_connectedUs.load()) / 1000, msSince(s_lastRxUs.load()), msSince(s_lastAckUs.load()),
              msSince(s_lastPublishUs.load()), (unsigned)s_published.load(), (unsigned)acked,
              (unsigned)(acked != 0 ? s_ackLatSumMs.load() / acked : 0), (unsigned)s_ackLatMaxMs.load(),
              oldestUnackedMs, (unsigned)s_paceWaitMaxMs.load());
    LOG_WARNING(TAG, "session: espnow rx=%u tx=%u txfail=%u, gateway msgs toMqtt=%u fromMqtt=%u",
              (unsigned)(now.espnowRx - s_baseline.espnowRx), (unsigned)(now.espnowTx - s_baseline.espnowTx),
              (unsigned)(now.espnowTxFailed - s_baseline.espnowTxFailed),
              (unsigned)(now.toRemote - s_baseline.toRemote), (unsigned)(now.fromRemote - s_baseline.fromRemote));
#if LWIP_STATS
    LOG_WARNING(TAG, "session: lwip tcp xmit=%u recv=%u drop=%u memerr=%u err=%u, link drop=%u memerr=%u",
              (unsigned)(now.tcpXmit - s_baseline.tcpXmit), (unsigned)(now.tcpRecv - s_baseline.tcpRecv),
              (unsigned)(now.tcpDrop - s_baseline.tcpDrop), (unsigned)(now.tcpMemErr - s_baseline.tcpMemErr),
              (unsigned)(now.tcpErr - s_baseline.tcpErr), (unsigned)(now.linkDrop - s_baseline.linkDrop),
              (unsigned)(now.linkMemErr - s_baseline.linkMemErr));
#endif
    if (s_wifiDumpOnDisconnect.load())
        esp_wifi_statis_dump(WIFI_STATIS_BUFFER | WIFI_STATIS_RXTX);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        LOG(TAG, "mqtt connected to broker");
        resetSessionDiagnostics();
        s_inflight.store(0); // esp-mqtt starts the session with an empty outbox
        s_session.fetch_add(1);
        s_connected.store(true, std::memory_order_release);
        s_sessionPending.store(true, std::memory_order_release);
        {
            NightMare::Message *marker = NULL; // wakes the publisher task if it is idle
            xQueueSend(s_outbound, &marker, 0);
        }
        break;

    case MQTT_EVENT_DISCONNECTED:
        LOG(TAG, "mqtt disconnected from broker");
        log_link_health("disconnect");
        log_session_summary();
        s_connected.store(false, std::memory_order_release);
        s_inflight.store(0);
        break;

    case MQTT_EVENT_PUBLISHED:
        noteAck(event->msg_id);
        if (s_inflight.load() > 0)
            s_inflight.fetch_sub(1);
        break;

    case MQTT_EVENT_DELETED:
        // A QoS1 message that went unacked past the outbox timeout is dropped by
        // esp-mqtt with no PUBACK. Without this its slot leaked, the window filled
        // for good and every publish waited out INFLIGHT_WAIT_MS until a reconnect.
        if (s_inflight.load() > 0)
            s_inflight.fetch_sub(1);
        break;

    case MQTT_EVENT_SUBSCRIBED:
        s_lastRxUs.store(nowUs());
        break;

    case MQTT_EVENT_DATA:
    {
        s_lastRxUs.store(nowUs());
        // A payload bigger than the client buffer arrives as several DATA
        // events and only the first one carries the topic, so reassemble here
        // instead of treating each event as a message of its own.
        if (event->current_data_offset == 0)
        {
            s_partial = NightMare::Message();
            s_partial.direction = NightMare::Direction::REMOTE_TO_LOCAL;
            s_partial.topic.assign(event->topic, event->topic_len);
            s_partial.persistent = event->retain;
            s_partial.payload.reserve(event->total_data_len);
            s_partialActive = true;
            if (isHardwareTopic(s_partial.topic) && event->total_data_len > event->data_len)
                HW_TRACE("mqtt rx start '%s' total=%d B retain=%d (arrives in fragments)", s_partial.topic.c_str(),
                         event->total_data_len, (int)event->retain);
        }
        if (!s_partialActive)
        {
            LOG_WARNING(TAG, "dropped %d B of a message joined mid-way (offset %d of %d)", event->data_len,
                      event->current_data_offset, event->total_data_len);
            break; // joined mid-message, wait for the next one to start cleanly
        }

        s_partial.payload.insert(s_partial.payload.end(), event->data, event->data + event->data_len);

        if ((int)s_partial.payload.size() >= event->total_data_len)
        {
            s_partialActive = false;
            if (isHardwareTopic(s_partial.topic))
                HW_TRACE("mqtt rx '%s' %u B retain=%d", s_partial.topic.c_str(), (unsigned)s_partial.payload.size(),
                         (int)s_partial.persistent);
            mqtt_onMessage(std::move(s_partial));
            s_partial = NightMare::Message();
        }
        break;
    }

    case MQTT_EVENT_ERROR:
        if (event->error_handle != NULL)
            LOG_ERROR(TAG, "MQTT error type=%d connack=%d tls_err=0x%x tls_stack=0x%x cert_flags=0x%x errno=%d",
                     event->error_handle->error_type, event->error_handle->connect_return_code,
                     event->error_handle->esp_tls_last_esp_err, event->error_handle->esp_tls_stack_err,
                     (unsigned)event->error_handle->esp_tls_cert_verify_flags,
                     event->error_handle->esp_transport_sock_errno);
        else
            LOG_ERROR(TAG, "MQTT error, no details");
        log_link_health("error");
        log_tcp_state("error");
        break;

    default:
        break;
    }
}

static void publisher_task(void *)
{
    uint32_t reportedDrops = 0;
    while (1)
    {
        NightMare::Message *message = NULL;
        if (xQueueReceive(s_outbound, &message, portMAX_DELAY) != pdTRUE)
            continue;

        // Before anything queued behind it, so the retained replay goes first.
        if (s_sessionPending.exchange(false, std::memory_order_acq_rel))
            run_session_setup();

        if (message == NULL)
            continue; // just the wake-up marker

        // Offline: drop rather than let esp-mqtt's outbox pile up QoS1 messages
        // for a broker that is gone. Anything persistent is in the vault anyway.
        const bool trace = isHardwareTopic(message->topic);
        const bool connected = s_connected.load(std::memory_order_acquire);
        const int id = connected ? publish_paced_id(s_client, *message) : -1;
        if (id < 0)
        {
            s_dropped.fetch_add(1);
            if (trace)
                HW_TRACE("mqtt tx '%s' %u B retain=%d NOT sent (%s)", message->topic.c_str(),
                         (unsigned)message->payload.size(), (int)message->persistent,
                         connected ? "publish window never freed or publish refused" : "broker offline");
        }
        else if (trace)
            HW_TRACE("mqtt tx '%s' %u B retain=%d id=%d", message->topic.c_str(), (unsigned)message->payload.size(),
                     (int)message->persistent, id);
        delete message;

        const uint32_t dropped = s_dropped.load();
        if (dropped != reportedDrops && uxQueueMessagesWaiting(s_outbound) == 0)
        {
            LOG_WARNING(TAG, "%lu publish(es) dropped (broker offline or unresponsive)",
                     (unsigned long)(dropped - reportedDrops));
            reportedDrops = dropped;
        }
    }
}

void mqtt_stop(void)
{
    if (s_client == NULL)
        return;
    if (s_connected.load(std::memory_order_acquire))
    {
        NightMare::Message offline;
        offline.topic = GatewayState::statusTopic();
        const std::string payload = GatewayState::statusJson(false);
        offline.payload.assign(payload.begin(), payload.end());
        offline.persistent = true;
        if (!mqtt_publish_immediate(s_client, offline))
            LOG_WARNING(TAG, "Failed to publish graceful gateway offline status");
    }
    esp_mqtt_client_stop(s_client);
    s_connected.store(false, std::memory_order_release);
    s_partialActive = false; // the client task is gone, so this is ours now
    s_partial = NightMare::Message();
    LOG_DEBUG(TAG, "MQTT stopped");
}

void mqtt_init(void)
{
    if (s_client != NULL) // stopped by mqtt_stop(): reconnect with the same client
    {
        LOG_DEBUG(TAG, "Reconnecting");
        ESP_ERROR_CHECK(esp_mqtt_client_start(s_client));
        return;
    }

    s_outbound = xQueueCreate(OUTBOUND_QUEUE_DEPTH, sizeof(NightMare::Message *));
    if (s_outbound == NULL)
    {
        LOG_ERROR(TAG, "Failed to allocate outbound queue");
        return;
    }
    s_tcpSnapDone = xSemaphoreCreateBinary(); // diagnostics only: log_tcp_state() skips itself without it

    esp_mqtt_client_config_t config = {};
    config.broker.address.hostname = s_settings.host.c_str();
    config.broker.address.port = s_settings.port;
    config.broker.address.transport = s_settings.tls ? MQTT_TRANSPORT_OVER_SSL : MQTT_TRANSPORT_OVER_TCP;
    if (!s_settings.caCertificate.empty())
        config.broker.verification.certificate = s_settings.caCertificate.c_str();
    if (!s_settings.username.empty())
        config.credentials.username = s_settings.username.c_str();
    if (!s_settings.password.empty())
        config.credentials.authentication.password = s_settings.password.c_str();
    s_brokerPort = (uint16_t)config.broker.address.port;
    config.session.protocol_ver = MQTT_PROTOCOL_V_5; // no_local is an MQTT 5 subscription option
    config.session.keepalive = 60;
    const std::string willTopic = GatewayState::statusTopic();
    const std::string willPayload = GatewayState::statusJson(false);
    config.session.last_will.topic = willTopic.c_str();
    config.session.last_will.msg = willPayload.c_str();
    config.session.last_will.msg_len = willPayload.size();
    config.session.last_will.qos = 1;
    config.session.last_will.retain = true;
    config.credentials.client_id = GatewayState::id().c_str();

    s_client = esp_mqtt_client_init(&config);
    if (s_client == NULL)
    {
        LOG_ERROR(TAG, "Failed to init MQTT client");
        return;
    }

    ESP_ERROR_CHECK(esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL));
    if (xTaskCreate(publisher_task, "MqttPublisher", PUBLISHER_STACK_SIZE, NULL,
                    PUBLISHER_PRIORITY, NULL) != pdPASS)
    {
        LOG_ERROR(TAG, "Failed to start publisher task");
        return;
    }
    ESP_ERROR_CHECK(esp_mqtt_client_start(s_client));
    LOG_DEBUG(TAG, "Connecting to %s:%d", config.broker.address.hostname,
             config.broker.address.port);
}

bool mqtt_publish(NightMare::Message message)
{
    if (s_client == NULL || s_outbound == NULL)
        return false;

    const bool trace = isHardwareTopic(message.topic);
    const std::string topic = trace ? message.topic : std::string();
    NightMare::Message *copy = new (std::nothrow) NightMare::Message(std::move(message));
    if (copy == nullptr)
    {
        s_dropped.fetch_add(1);
        LOG_ERROR(TAG, "Unable to allocate outbound publish envelope");
        if (trace)
            HW_TRACE("mqtt queue '%s' DROPPED: no memory for the envelope", topic.c_str());
        return false;
    }
    if (xQueueSend(s_outbound, &copy, 0) != pdTRUE)
    {
        delete copy;
        s_dropped.fetch_add(1);
        if (trace)
            HW_TRACE("mqtt queue '%s' DROPPED: outbound queue full", topic.c_str());
        return false;
    }
    return true;
}

bool mqtt_publish_immediate(esp_mqtt_client_handle_t client,
                            const NightMare::Message &message)
{
    return client != NULL && publish_paced(client, message);
}

bool mqtt_is_connected(void)
{
    return s_connected.load(std::memory_order_acquire);
}

#endif // NM_GATEWAY_MQTT
