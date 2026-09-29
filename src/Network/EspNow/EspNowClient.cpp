#include <NightMare/Features.h>
#if NM_NETWORK_ESPNOW

#include "EspNowClient.h"
#include "NightMareEspNow/Frame.h"

#include <esp_log.h>
#include <esp_now.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <cstring>
#include <string>
#include <vector>

namespace NightMare::EspNowClient
{
namespace
{
constexpr char Tag[] = "EspNowClient";
constexpr size_t FrameHeaderSize = sizeof(FrameHeader);
constexpr size_t MaxFramesPerMessage = 16; // what the gateway reassembles
constexpr size_t MaxTopicLength = 63;      // 6-bit length in the message encoding
constexpr size_t MaxSubscriptions = 16;
constexpr uint32_t SearchIntervalMs = 3000; // probe rate until a gateway answers
constexpr uint32_t SendTimeoutMs = 50;
constexpr uint32_t ReassemblyTimeoutMs = 5000;
constexpr uint8_t BroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// `lock` (spinlock, tiny critical sections only) guards the connection fields;
// `listMutex` guards the subscription list and last will; `sendMutex`
// serialises transmissions, since ESP-NOW allows one in flight.
portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
SemaphoreHandle_t listMutex = nullptr;
SemaphoreHandle_t sendMutex = nullptr;
SemaphoreHandle_t sendDone = nullptr;
TaskHandle_t task = nullptr;
volatile bool running = false;
volatile bool taskExited = true;

Settings settings;
State currentState = State::STOPPED;
bool gatewayKnown = false;
uint8_t gatewayMac[6] = {};
uint8_t missed = 0;
bool needResync = false;
uint16_t probeId = 0;
uint64_t probeSentUs = 0;
uint32_t lastRttMs = 0;
uint16_t nextMessageId = 1;
StateCallback stateCallback = nullptr;
MessageCallback messageCallback = nullptr;

std::vector<std::string> subscriptions;
bool willSet = false;
std::vector<uint8_t> willRaw;

struct Reassembly
{
    bool active = false;
    uint16_t messageId = 0;
    uint16_t nextFrame = 0;
    uint16_t totalFrames = 0;
    uint64_t startedUs = 0;
    std::vector<uint8_t> buffer;
} reassembly;

uint64_t nowUs() { return static_cast<uint64_t>(esp_timer_get_time()); }

class Guard
{
public:
    explicit Guard(SemaphoreHandle_t m) : m_(m)
    {
        if (m_ != nullptr)
            xSemaphoreTake(m_, portMAX_DELAY);
    }
    ~Guard()
    {
        if (m_ != nullptr)
            xSemaphoreGive(m_);
    }
    Guard(const Guard &) = delete;
    Guard &operator=(const Guard &) = delete;

private:
    SemaphoreHandle_t m_;
};

bool ensureMutexes()
{
    portENTER_CRITICAL(&lock);
    const bool needed = listMutex == nullptr || sendMutex == nullptr || sendDone == nullptr;
    portEXIT_CRITICAL(&lock);
    if (!needed)
        return true;
    // Only called from begin()/the list setters before concurrent use.
    if (listMutex == nullptr)
        listMutex = xSemaphoreCreateMutex();
    if (sendMutex == nullptr)
        sendMutex = xSemaphoreCreateMutex();
    if (sendDone == nullptr)
        sendDone = xSemaphoreCreateBinary();
    return listMutex != nullptr && sendMutex != nullptr && sendDone != nullptr;
}

void setState(State next)
{
    StateCallback callback;
    portENTER_CRITICAL(&lock);
    const bool changed = currentState != next;
    currentState = next;
    callback = stateCallback;
    portEXIT_CRITICAL(&lock);
    if (changed && callback != nullptr)
        callback(next);
}

void sendCallback(const esp_now_send_info_t *, esp_now_send_status_t)
{
    if (sendDone != nullptr)
        xSemaphoreGive(sendDone);
}

bool sendRaw(const uint8_t *mac, const Frame &frame)
{
    if (frame.header.length > sizeof(frame.data))
        return false;
    Guard guard(sendMutex);
    xSemaphoreTake(sendDone, 0); // drop a stale completion from a timed-out send
    const esp_err_t err = esp_now_send(mac, reinterpret_cast<const uint8_t *>(&frame),
                                       FrameHeaderSize + frame.header.length);
    if (err != ESP_OK)
    {
        ESP_LOGW(Tag, "send failed: %s", esp_err_to_name(err));
        return false;
    }
    return xSemaphoreTake(sendDone, pdMS_TO_TICKS(SendTimeoutMs)) == pdTRUE;
}

bool gatewayAddress(uint8_t out[6])
{
    portENTER_CRITICAL(&lock);
    const bool known = gatewayKnown;
    if (known)
        memcpy(out, gatewayMac, 6);
    portEXIT_CRITICAL(&lock);
    return known;
}

uint16_t newMessageId()
{
    portENTER_CRITICAL(&lock);
    const uint16_t id = nextMessageId++;
    if (nextMessageId == 0)
        nextMessageId = 1;
    portEXIT_CRITICAL(&lock);
    return id;
}

bool sendControl(FrameType type, const uint8_t *data, size_t length)
{
    uint8_t mac[6];
    if (length > MaxFrameDataSize || !gatewayAddress(mac))
        return false;
    Frame frame{};
    frame.header = encodeFrameHeader(newMessageId(), VersionType::ESP_NOW, type, 0, 1,
                                     static_cast<uint16_t>(length));
    if (length > 0)
        memcpy(frame.data, data, length);
    return sendRaw(mac, frame);
}

// [retained:1][reserved:1][topicLength:6][topic][payload]
bool encodeMessage(std::vector<uint8_t> &out, const char *topic, const uint8_t *payload,
                   size_t length, bool retained)
{
    if (topic == nullptr || (payload == nullptr && length != 0))
        return false;
    const size_t topicLength = strlen(topic);
    if (topicLength == 0 || topicLength > MaxTopicLength ||
        strpbrk(topic, "+#") != nullptr)
        return false;
    out.clear();
    out.reserve(1 + topicLength + length);
    out.push_back(static_cast<uint8_t>((retained ? 0x80 : 0) | topicLength));
    out.insert(out.end(), topic, topic + topicLength);
    if (length > 0)
        out.insert(out.end(), payload, payload + length);
    return true;
}

bool sendMessageFrames(const std::vector<uint8_t> &raw)
{
    uint8_t mac[6];
    if (!gatewayAddress(mac))
        return false;
    const size_t totalFrames = (raw.size() + MaxFrameDataSize - 1) / MaxFrameDataSize;
    if (totalFrames == 0 || totalFrames > MaxFramesPerMessage)
        return false;
    const uint16_t id = newMessageId();
    for (size_t i = 0; i < totalFrames; ++i)
    {
        const size_t offset = i * MaxFrameDataSize;
        const size_t chunk = raw.size() - offset < MaxFrameDataSize ? raw.size() - offset
                                                                     : MaxFrameDataSize;
        Frame frame{};
        frame.header = encodeFrameHeader(id, VersionType::ESP_NOW, FrameType::MESSAGE,
                                         static_cast<uint16_t>(i),
                                         static_cast<uint16_t>(totalFrames),
                                         static_cast<uint16_t>(chunk));
        memcpy(frame.data, raw.data() + offset, chunk);
        if (!sendRaw(mac, frame))
            return false;
    }
    return true;
}

void sendHeartbeat()
{
    uint8_t mac[6];
    const bool known = gatewayAddress(mac);
    const uint16_t id = newMessageId();
    portENTER_CRITICAL(&lock);
    probeId = id;
    probeSentUs = nowUs();
    if (known)
        ++missed;
    portEXIT_CRITICAL(&lock);
    // Unknown gateway: probe by broadcast. The gateway registers any sender and
    // answers from its own address, which is how we learn it.
    sendRaw(known ? mac : BroadcastMac, keepAliveFrame(id));
}

void resync()
{
    std::vector<std::string> filters;
    std::vector<uint8_t> will;
    {
        Guard guard(listMutex);
        filters = subscriptions;
        if (willSet)
            will = willRaw;
    }
    for (const std::string &filter : filters)
        sendControl(FrameType::SUBSCRIBE, reinterpret_cast<const uint8_t *>(filter.data()),
                    filter.size());
    if (!will.empty())
        sendControl(FrameType::LAST_WILL, will.data(), will.size());
}

void setGateway(const uint8_t *mac)
{
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, mac, 6);
    peer.channel = 0; // follow the station's channel
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = false;
    const esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST)
    {
        ESP_LOGE(Tag, "add gateway peer failed: %s", esp_err_to_name(err));
        return;
    }
    portENTER_CRITICAL(&lock);
    memcpy(gatewayMac, mac, 6);
    gatewayKnown = true;
    missed = 0;
    portEXIT_CRITICAL(&lock);
    ESP_LOGI(Tag, "gateway %02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
}

void forgetGateway()
{
    uint8_t mac[6];
    if (!gatewayAddress(mac))
        return;
    esp_now_del_peer(mac);
    portENTER_CRITICAL(&lock);
    gatewayKnown = false;
    portEXIT_CRITICAL(&lock);
}

void wakeTask()
{
    if (task != nullptr)
        xTaskNotifyGive(task);
}

void handleMessageFrame(const Frame &frame)
{
    const FrameHeader &h = frame.header;
    if (h.totalFrames == 0 || h.totalFrames > MaxFramesPerMessage)
        return;

    const uint8_t *data = frame.data;
    size_t length = h.length;
    std::vector<uint8_t> whole;
    if (h.totalFrames > 1)
    {
        Reassembly &r = reassembly;
        if (r.active && (nowUs() - r.startedUs) / 1000 > ReassemblyTimeoutMs)
            r.active = false;
        if (!r.active || r.messageId != h.messageId)
        {
            if (h.frameIndex != 0)
                return; // joined mid-message
            r.active = true;
            r.messageId = h.messageId;
            r.nextFrame = 0;
            r.totalFrames = h.totalFrames;
            r.startedUs = nowUs();
            r.buffer.clear();
        }
        if (h.frameIndex != r.nextFrame)
        {
            r.active = false; // out of order: drop the message
            return;
        }
        r.buffer.insert(r.buffer.end(), frame.data, frame.data + h.length);
        if (++r.nextFrame < r.totalFrames)
            return;
        r.active = false;
        whole.swap(r.buffer);
        data = whole.data();
        length = whole.size();
    }

    if (length < 1)
        return;
    const size_t topicLength = data[0] & 0x3F;
    if (topicLength == 0 || length < 1 + topicLength)
        return;
    MessageCallback callback;
    portENTER_CRITICAL(&lock);
    callback = messageCallback;
    portEXIT_CRITICAL(&lock);
    if (callback == nullptr)
        return;
    const std::string topic(reinterpret_cast<const char *>(data + 1), topicLength);
    callback(topic.c_str(), data + 1 + topicLength, length - 1 - topicLength,
             (data[0] & 0x80) != 0);
}

void receiveCallback(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    if (!running || info == nullptr || data == nullptr || len <= 0)
        return;
    FrameHeader header;
    if (!decodeFrameHeader(header, data, static_cast<size_t>(len)) ||
        header.version != static_cast<uint8_t>(VersionType::ESP_NOW))
        return;
    Frame frame{};
    frame.header = header;
    if (header.length > sizeof(frame.data))
        return;
    memcpy(frame.data, data + FrameHeaderSize, header.length);

    uint8_t gateway[6];
    const bool known = gatewayAddress(gateway);
    const bool fromGateway = known && memcmp(gateway, info->src_addr, 6) == 0;

    switch (static_cast<FrameType>(header.type))
    {
    case FrameType::BEACON:
    {
        // data: [count][versions...]; only adopt a gateway that speaks our version.
        bool speaksUs = false;
        if (header.length >= 1)
            for (size_t i = 0; i < frame.data[0] && i + 1 < header.length; ++i)
                speaksUs |= frame.data[1 + i] == static_cast<uint8_t>(VersionType::ESP_NOW);
        if (!known && speaksUs)
        {
            setGateway(info->src_addr);
            wakeTask(); // register right away instead of waiting for the next tick
        }
        break;
    }
    case FrameType::ACK:
    {
        uint16_t expectedProbe;
        uint64_t sentUs;
        portENTER_CRITICAL(&lock);
        expectedProbe = probeId;
        sentUs = probeSentUs;
        portEXIT_CRITICAL(&lock);
        // Learn the gateway from the answer to our broadcast probe, and only that.
        if (!known && header.messageId == expectedProbe)
            setGateway(info->src_addr);
        else if (!fromGateway)
            break;
        portENTER_CRITICAL(&lock);
        missed = 0;
        if (header.messageId == probeId)
            lastRttMs = static_cast<uint32_t>((nowUs() - sentUs) / 1000);
        const bool wasConnected = currentState == State::CONNECTED;
        if (!wasConnected)
            needResync = true;
        portEXIT_CRITICAL(&lock);
        if (!wasConnected)
        {
            setState(State::CONNECTED);
            wakeTask(); // resync subscriptions/will now
        }
        break;
    }
    case FrameType::MESSAGE:
        if (fromGateway)
            handleMessageFrame(frame);
        break;
    default:
        break;
    }
}

void clientTask(void *)
{
    while (running)
    {
        // Connected: one heartbeat per interval. Otherwise probe quickly.
        const uint32_t waitMs =
            state() == State::CONNECTED ? settings.heartbeatMs : SearchIntervalMs;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));
        if (!running)
            break;

        bool resyncNow;
        bool lost;
        portENTER_CRITICAL(&lock);
        resyncNow = needResync && currentState == State::CONNECTED;
        if (resyncNow)
            needResync = false;
        lost = gatewayKnown && missed >= settings.missedBeforeLost;
        portEXIT_CRITICAL(&lock);

        if (resyncNow)
        {
            resync();
            continue; // that wake-up was for a resync, not a heartbeat tick
        }
        if (lost)
        {
            ESP_LOGW(Tag, "gateway silent, searching again");
            forgetGateway();
            setState(State::SEARCHING);
        }
        sendHeartbeat();
    }
    taskExited = true;
    task = nullptr;
    vTaskDelete(nullptr);
}
}

bool begin(const Settings &config)
{
    if (running)
        return true;
    if (config.heartbeatMs == 0 || config.missedBeforeLost == 0 || !ensureMutexes())
        return false;
    settings = config;

    const esp_err_t err = esp_now_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(Tag, "esp_now_init failed: %s", esp_err_to_name(err));
        return false;
    }
    esp_now_register_recv_cb(receiveCallback);
    esp_now_register_send_cb(sendCallback);

    esp_now_peer_info_t broadcast = {};
    memcpy(broadcast.peer_addr, BroadcastMac, 6);
    broadcast.ifidx = WIFI_IF_STA;
    broadcast.encrypt = false;
    esp_now_add_peer(&broadcast);

    portENTER_CRITICAL(&lock);
    gatewayKnown = false;
    missed = 0;
    needResync = false;
    lastRttMs = 0;
    portEXIT_CRITICAL(&lock);
    reassembly = Reassembly();

    running = true;
    taskExited = false;
    if (xTaskCreate(clientTask, "espnow_client", 3072, nullptr, 1, &task) != pdPASS)
    {
        running = false;
        taskExited = true;
        esp_now_unregister_recv_cb();
        esp_now_unregister_send_cb();
        esp_now_deinit();
        return false;
    }
    setState(State::SEARCHING);
    return true;
}

void end()
{
    if (!running)
        return;
    running = false;
    wakeTask();
    for (int i = 0; i < 100 && !taskExited; ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    esp_now_deinit();
    portENTER_CRITICAL(&lock);
    gatewayKnown = false;
    portEXIT_CRITICAL(&lock);
    setState(State::STOPPED);
}

State state()
{
    portENTER_CRITICAL(&lock);
    const State s = currentState;
    portEXIT_CRITICAL(&lock);
    return s;
}

uint32_t rttMs()
{
    portENTER_CRITICAL(&lock);
    const uint32_t v = lastRttMs;
    portEXIT_CRITICAL(&lock);
    return v;
}

void onState(StateCallback callback)
{
    portENTER_CRITICAL(&lock);
    stateCallback = callback;
    portEXIT_CRITICAL(&lock);
}

void onMessage(MessageCallback callback)
{
    portENTER_CRITICAL(&lock);
    messageCallback = callback;
    portEXIT_CRITICAL(&lock);
}

bool subscribe(const char *filter)
{
    if (filter == nullptr || filter[0] == '\0' || strlen(filter) > MaxFrameDataSize ||
        !ensureMutexes())
        return false;
    {
        Guard guard(listMutex);
        bool present = false;
        for (const std::string &s : subscriptions)
            present |= s == filter;
        if (!present)
        {
            if (subscriptions.size() >= MaxSubscriptions)
                return false;
            subscriptions.emplace_back(filter);
        }
    }
    if (state() == State::CONNECTED)
        sendControl(FrameType::SUBSCRIBE, reinterpret_cast<const uint8_t *>(filter),
                    strlen(filter));
    return true;
}

bool unsubscribe(const char *filter)
{
    if (filter == nullptr || !ensureMutexes())
        return false;
    bool removed = false;
    {
        Guard guard(listMutex);
        for (size_t i = 0; i < subscriptions.size(); ++i)
            if (subscriptions[i] == filter)
            {
                subscriptions.erase(subscriptions.begin() + i);
                removed = true;
                break;
            }
    }
    if (removed && state() == State::CONNECTED)
        sendControl(FrameType::UNSUBSCRIBE, reinterpret_cast<const uint8_t *>(filter),
                    strlen(filter));
    return removed;
}

bool setLastWill(const char *topic, const uint8_t *payload, size_t length, bool retained)
{
    std::vector<uint8_t> raw;
    if (!encodeMessage(raw, topic, payload, length, retained) ||
        raw.size() > MaxFrameDataSize || !ensureMutexes())
        return false;
    {
        Guard guard(listMutex);
        willRaw = raw;
        willSet = true;
    }
    if (state() == State::CONNECTED)
        sendControl(FrameType::LAST_WILL, raw.data(), raw.size());
    return true;
}

bool publish(const char *topic, const uint8_t *payload, size_t length, bool retained)
{
    if (state() != State::CONNECTED)
        return false;
    std::vector<uint8_t> raw;
    return encodeMessage(raw, topic, payload, length, retained) && sendMessageFrames(raw);
}
}

#endif // NM_NETWORK_ESPNOW
