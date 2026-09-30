#include <NightMare/Features.h>
#if NM_NETWORK_ESPNOW

#include "EspNowClient.h"
#include "NightMareEspNow/Frame.h"
#include "Core/Logs.h"
#include <esp_now.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace NightMare::EspNowClient
{
    namespace
    {
        // NightMare's LOG, not ESP_LOGx: the Arduino core hides ESP_LOG output,
        // which is how every failure in here used to vanish.
        constexpr char TagLink[] = "ESPNOW";  // search, gateway, connection state
        constexpr char TagTx[] = "ESPNOW-TX"; // outgoing frames
        constexpr char TagRx[] = "ESPNOW-RX"; // incoming frames
        constexpr size_t FrameHeaderSize = sizeof(FrameHeader);
        constexpr size_t MaxFramesPerMessage = 16; // what the gateway reassembles
        constexpr size_t MaxTopicLength = 63;      // 6-bit length in the message encoding
        constexpr size_t MaxSubscriptions = 16;
        constexpr uint32_t SearchIntervalMs = 3000; // probe rate until a gateway answers
        constexpr uint32_t HopIntervalMs = 300;     // dwell per channel while hopping
        constexpr uint8_t LastHopChannel = 13;
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

        // Set by the send callback, read after sendDone: whether the peer
        // MAC-acknowledged the last unicast (a broadcast always reports success).
        volatile esp_now_send_status_t lastSendStatus = ESP_NOW_SEND_SUCCESS;

        struct MacText
        {
            char text[18];
        };

        MacText macText(const uint8_t *mac)
        {
            MacText out;
            snprintf(out.text, sizeof(out.text), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1],
                     mac[2], mac[3], mac[4], mac[5]);
            return out;
        }

        uint8_t currentChannel()
        {
            uint8_t primary = 0;
            wifi_second_chan_t second;
            esp_wifi_get_channel(&primary, &second);
            return primary;
        }

        const char *frameTypeName(uint8_t type)
        {
            switch (static_cast<FrameType>(type))
            {
            case FrameType::BEACON: return "BEACON";
            case FrameType::AUTH: return "AUTH";
            case FrameType::SUBSCRIBE: return "SUBSCRIBE";
            case FrameType::UNSUBSCRIBE: return "UNSUBSCRIBE";
            case FrameType::MESSAGE: return "MESSAGE";
            case FrameType::CONTROL: return "CONTROL";
            case FrameType::ACK: return "ACK";
            case FrameType::ERROR: return "ERROR";
            case FrameType::LAST_WILL: return "LAST_WILL";
            }
            return "?";
        }

        // The receive callback runs on the Wi-Fi task, which has no stack to spare for
        // formatting a log line (NMLog + Serial.printf, the same thing that overflowed
        // sys_evt). It only queues plain data here; rxLogTask formats and prints it.
        enum class RxNote : uint8_t
        {
            BadHeader,      // value: received length
            WrongVersion,
            TooLong,
            BeaconAdopted,
            BeaconIgnored,  // does not speak our version
            BeaconKnown,
            AckForProbe,
            AckIgnored,     // value: 1 = not our gateway, 0 = not the current probe
            AckRtt,         // value: rtt ms
            Connected,      // value: rtt ms
            GatewayLearned, // value: channel
            AddPeerFailed,  // value: esp_err_t
            MessageIgnored,
            ErrorFrame,
            Unhandled,
        };

        struct RxEvent
        {
            RxNote note;
            uint8_t type;
            uint8_t version;
            int8_t rssi;
            uint16_t messageId;
            uint16_t length;
            uint8_t mac[6];
            int32_t value;
        };

        constexpr UBaseType_t RxLogDepth = 16;
        QueueHandle_t rxLogQueue = nullptr;
        TaskHandle_t rxLogTask = nullptr;

        void note(RxNote what, const uint8_t *mac, const FrameHeader *header = nullptr,
                  int rssi = 0, int32_t value = 0)
        {
            if (rxLogQueue == nullptr)
                return;
            RxEvent event{};
            event.note = what;
            if (mac != nullptr)
                memcpy(event.mac, mac, 6);
            if (header != nullptr)
            {
                event.type = header->type;
                event.version = header->version;
                event.messageId = header->messageId;
                event.length = header->length;
            }
            event.rssi = static_cast<int8_t>(rssi);
            event.value = value;
            xQueueSend(rxLogQueue, &event, 0); // full: drop the note, never stall the Wi-Fi task
        }

        void printRxEvent(const RxEvent &e)
        {
            const MacText from = macText(e.mac);
            const char *type = frameTypeName(e.type);
            switch (e.note)
            {
            case RxNote::BadHeader:
                LOG_WARNING(TagRx, "%ld bytes from %s: not a valid frame header",
                            static_cast<long>(e.value), from.text);
                break;
            case RxNote::WrongVersion:
                LOG_WARNING(TagRx, "%s #%u from %s: version %u, expected %u", type, e.messageId,
                            from.text, e.version, static_cast<unsigned>(VersionType::ESP_NOW));
                break;
            case RxNote::TooLong:
                LOG_WARNING(TagRx, "%s #%u from %s: %u data bytes exceed a frame", type,
                            e.messageId, from.text, e.length);
                break;
            case RxNote::BeaconAdopted:
                LOG(TagRx, "BEACON from %s (rssi %d), adopting it as gateway", from.text, e.rssi);
                break;
            case RxNote::BeaconIgnored:
                LOG_WARNING(TagRx, "BEACON from %s (rssi %d) does not list version %u, ignored",
                            from.text, e.rssi, static_cast<unsigned>(VersionType::ESP_NOW));
                break;
            case RxNote::BeaconKnown:
                LOG_DEBUG(TagRx, "BEACON from %s (rssi %d)", from.text, e.rssi);
                break;
            case RxNote::AckForProbe:
                LOG(TagRx, "ACK #%u from %s (rssi %d) answers our probe", e.messageId, from.text,
                    e.rssi);
                break;
            case RxNote::AckIgnored:
                LOG_WARNING(TagRx, "ACK #%u from %s ignored: %s", e.messageId, from.text,
                            e.value != 0 ? "not our gateway" : "not the current probe");
                break;
            case RxNote::AckRtt:
                LOG_DEBUG(TagRx, "ACK #%u, rtt %ld ms", e.messageId, static_cast<long>(e.value));
                break;
            case RxNote::Connected:
                LOG(TagLink, "connected to gateway %s (rtt %ld ms)", from.text,
                    static_cast<long>(e.value));
                break;
            case RxNote::GatewayLearned:
                LOG(TagLink, "gateway %s on ch %ld", from.text, static_cast<long>(e.value));
                break;
            case RxNote::AddPeerFailed:
                LOG_ERROR(TagLink, "add gateway peer %s failed: %s", from.text,
                          esp_err_to_name(static_cast<esp_err_t>(e.value)));
                break;
            case RxNote::MessageIgnored:
                LOG_WARNING(TagRx, "MESSAGE #%u from %s ignored: not our gateway", e.messageId,
                            from.text);
                break;
            case RxNote::ErrorFrame:
                LOG_WARNING(TagRx, "ERROR #%u from %s: the gateway rejected that request",
                            e.messageId, from.text);
                break;
            case RxNote::Unhandled:
                LOG_DEBUG(TagRx, "%s #%u from %s: not handled", type, e.messageId, from.text);
                break;
            }
        }

        // Created once and never torn down: it only blocks on its queue, and keeping it
        // alive avoids a race with a quick end()/begin() leaving no printer running.
        void rxLogTaskMain(void *)
        {
            RxEvent event;
            for (;;)
                if (xQueueReceive(rxLogQueue, &event, portMAX_DELAY) == pdTRUE)
                    printRxEvent(event);
        }

        bool ensureRxLog()
        {
            if (rxLogQueue == nullptr)
                rxLogQueue = xQueueCreate(RxLogDepth, sizeof(RxEvent));
            if (rxLogQueue == nullptr)
                return false;
            if (rxLogTask == nullptr &&
                xTaskCreate(rxLogTaskMain, "espnow_rxlog", 3584, nullptr, 1, &rxLogTask) != pdPASS)
                rxLogTask = nullptr;
            return rxLogTask != nullptr;
        }

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

        void sendCallback(const esp_now_send_info_t *, esp_now_send_status_t status)
        {
            lastSendStatus = status;
            if (sendDone != nullptr)
                xSemaphoreGive(sendDone);
        }

        bool sendRaw(const uint8_t *mac, const Frame &frame)
        {
            if (frame.header.length > sizeof(frame.data))
            {
                LOG_ERROR(TagTx, "%s #%u: %u data bytes exceed a frame",
                          frameTypeName(frame.header.type), frame.header.messageId,
                          frame.header.length);
                return false;
            }
            Guard guard(sendMutex);
            xSemaphoreTake(sendDone, 0); // drop a stale completion from a timed-out send
            const esp_err_t err = esp_now_send(mac, reinterpret_cast<const uint8_t *>(&frame),
                                               FrameHeaderSize + frame.header.length);
            if (err != ESP_OK)
            {
                LOG_WARNING(TagTx, "%s #%u to %s on ch %u: esp_now_send failed: %s",
                            frameTypeName(frame.header.type), frame.header.messageId,
                            macText(mac).text, currentChannel(), esp_err_to_name(err));
                return false;
            }
            if (xSemaphoreTake(sendDone, pdMS_TO_TICKS(SendTimeoutMs)) != pdTRUE)
            {
                LOG_WARNING(TagTx, "%s #%u to %s: no send completion within %lu ms",
                            frameTypeName(frame.header.type), frame.header.messageId,
                            macText(mac).text, static_cast<unsigned long>(SendTimeoutMs));
                return false;
            }
            // Reported, not acted on: the return value keeps meaning "the radio took it".
            if (lastSendStatus != ESP_NOW_SEND_SUCCESS)
                LOG_WARNING(TagTx, "%s #%u to %s: not acknowledged by the peer",
                            frameTypeName(frame.header.type), frame.header.messageId,
                            macText(mac).text);
            return true;
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
            if (known)
                LOG_DEBUG(TagTx, "heartbeat #%u to gateway on ch %u (unanswered so far: %u)", id,
                          currentChannel(), missed);
            else
                LOG(TagTx, "probe #%u broadcast on ch %u", id, currentChannel());
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
            LOG(TagLink, "resync: %u subscription(s)%s", static_cast<unsigned>(filters.size()),
                will.empty() ? "" : " + last will");
            for (const std::string &filter : filters)
                if (!sendControl(FrameType::SUBSCRIBE,
                                 reinterpret_cast<const uint8_t *>(filter.data()), filter.size()))
                    LOG_WARNING(TagLink, "resync: could not send SUBSCRIBE '%s'", filter.c_str());
            if (!will.empty() && !sendControl(FrameType::LAST_WILL, will.data(), will.size()))
                LOG_WARNING(TagLink, "resync: could not send LAST_WILL");
        }

        void setGateway(const uint8_t *mac)
        {
            esp_now_peer_info_t peer = {};
            memcpy(peer.peer_addr, mac, 6);
            peer.channel = 0; // follow the station's channel
            peer.ifidx = WIFI_IF_STA;
            peer.encrypt = false;
            const esp_err_t err = esp_now_add_peer(&peer);
            // Called from the receive callback (the Wi-Fi task): note(), never LOG, here.
            if (err != ESP_OK && err != ESP_ERR_ESPNOW_EXIST)
            {
                note(RxNote::AddPeerFailed, mac, nullptr, 0, err);
                return;
            }
            portENTER_CRITICAL(&lock);
            memcpy(gatewayMac, mac, 6);
            gatewayKnown = true;
            missed = 0;
            portEXIT_CRITICAL(&lock);
            note(RxNote::GatewayLearned, mac, nullptr, 0, currentChannel());
        }

        void forgetGateway()
        {
            uint8_t mac[6];
            if (!gatewayAddress(mac))
                return;
            LOG(TagLink, "forgetting gateway %s", macText(mac).text);
            esp_now_del_peer(mac);
            portENTER_CRITICAL(&lock);
            gatewayKnown = false;
            portEXIT_CRITICAL(&lock);
        }

        // True only when the station has no AP to follow, so the client may pick the channel.
        // An associated station is pinned to its AP's channel. One with an AP configured but not
        // associated yet is mid-handshake (or retrying): retuning the radio under it every hop
        // breaks the join, the Wi-Fi service retries, the next hop breaks that too, and neither
        // ever finishes. Leave the channel to the AP in both cases -- the gateway is expected on
        // the same one.
        bool freeToTune()
        {
            wifi_config_t config = {};
            if (esp_wifi_get_config(WIFI_IF_STA, &config) == ESP_OK && config.sta.ssid[0] != '\0')
                return false;
            wifi_ap_record_t ap;
            return esp_wifi_sta_get_ap_info(&ap) != ESP_OK;
        }

        // Whether the search should step through channels (no AP pins the radio, none is fixed).
        bool hoppingNow()
        {
            return settings.channel == 0 && freeToTune();
        }

        // Moves the radio to the next search channel, or the fixed one, when not pinned to an AP.
        void tuneForSearch()
        {
            if (!freeToTune())
                return;
            static uint8_t channel = 0;
            channel = settings.channel != 0 ? settings.channel
                                            : (channel >= LastHopChannel ? 1 : channel + 1);
            const esp_err_t err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
            if (err != ESP_OK)
                LOG_WARNING(TagLink, "could not tune to ch %u: %s", channel, esp_err_to_name(err));
            else
                LOG(TagLink, "searching on ch %u", channel);
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
            // Runs on the Wi-Fi task: note(), never LOG -- see RxNote.
            const uint8_t *from = info->src_addr;
            const int rssi = info->rx_ctrl != nullptr ? info->rx_ctrl->rssi : 0;
            FrameHeader header;
            if (!decodeFrameHeader(header, data, static_cast<size_t>(len)))
            {
                note(RxNote::BadHeader, from, nullptr, rssi, len);
                return;
            }
            if (header.version != static_cast<uint8_t>(VersionType::ESP_NOW))
            {
                note(RxNote::WrongVersion, from, &header, rssi);
                return;
            }
            Frame frame{};
            frame.header = header;
            if (header.length > sizeof(frame.data))
            {
                note(RxNote::TooLong, from, &header, rssi);
                return;
            }
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
                if (known)
                {
                    note(RxNote::BeaconKnown, from, &header, rssi);
                }
                else if (!speaksUs)
                {
                    note(RxNote::BeaconIgnored, from, &header, rssi);
                }
                else
                {
                    note(RxNote::BeaconAdopted, from, &header, rssi);
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
                {
                    note(RxNote::AckForProbe, from, &header, rssi);
                    setGateway(info->src_addr);
                }
                else if (!fromGateway)
                {
                    note(RxNote::AckIgnored, from, &header, rssi, known ? 1 : 0);
                    break;
                }
                portENTER_CRITICAL(&lock);
                missed = 0;
                const bool answersProbe = header.messageId == probeId;
                if (answersProbe)
                    lastRttMs = static_cast<uint32_t>((nowUs() - sentUs) / 1000);
                const uint32_t rtt = lastRttMs;
                const bool wasConnected = currentState == State::CONNECTED;
                if (!wasConnected)
                    needResync = true;
                portEXIT_CRITICAL(&lock);
                if (answersProbe)
                    note(RxNote::AckRtt, from, &header, rssi, static_cast<int32_t>(rtt));
                if (!wasConnected)
                {
                    note(RxNote::Connected, from, &header, rssi, static_cast<int32_t>(rtt));
                    setState(State::CONNECTED);
                    wakeTask(); // resync subscriptions/will now
                }
                break;
            }
            case FrameType::MESSAGE:
                if (fromGateway)
                    handleMessageFrame(frame);
                else
                    note(RxNote::MessageIgnored, from, &header, rssi);
                break;
            case FrameType::ERROR:
                note(RxNote::ErrorFrame, from, &header, rssi);
                break;
            default:
                note(RxNote::Unhandled, from, &header, rssi);
                break;
            }
        }

        void clientTask(void *)
        {
            // 0 = not searching, 1 = hopping channels, 2 = held on the AP's (or the fixed) channel
            uint8_t lastMode = 0xFF;
            while (running)
            {
                // Connected: one heartbeat per interval. Otherwise probe quickly.
                uint8_t knownMac[6];
                const bool searching = state() != State::CONNECTED && !gatewayAddress(knownMac);
                const bool hopping = searching && hoppingNow();
                const uint8_t mode = !searching ? 0 : hopping ? 1 : 2;
                if (mode != lastMode)
                {
                    lastMode = mode;
                    if (mode == 1)
                        LOG(TagLink, "searching: hopping ch 1..%u (no AP configured)", LastHopChannel);
                    else if (mode == 2)
                        LOG(TagLink, "searching: probing on ch %u every %lu ms (%s)",
                            currentChannel(), static_cast<unsigned long>(SearchIntervalMs),
                            settings.channel != 0 ? "fixed channel" : "channel follows the AP");
                }
                const uint32_t waitMs = state() == State::CONNECTED ? settings.heartbeatMs
                                        : hopping                   ? HopIntervalMs
                                                                    : SearchIntervalMs;
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));
                if (!running)
                    break;
                // Tune first, probe second, then wait a full dwell for the answer on that channel.
                if (searching)
                    tuneForSearch();

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
                    LOG_WARNING(TagLink, "gateway silent for %u heartbeats, searching again",
                                settings.missedBeforeLost);
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

        uint8_t ownMac[6] = {};
        esp_wifi_get_mac(WIFI_IF_STA, ownMac);
        wifi_config_t station = {};
        const bool haveApConfig = esp_wifi_get_config(WIFI_IF_STA, &station) == ESP_OK &&
                                  station.sta.ssid[0] != '\0';
        LOG(TagLink, "starting on %s, ch %u, heartbeat %lu ms, lost after %u, AP: %s", macText(ownMac).text,
            currentChannel(), static_cast<unsigned long>(config.heartbeatMs), config.missedBeforeLost,
            haveApConfig ? reinterpret_cast<const char *>(station.sta.ssid) : "(none)");

        if (!ensureRxLog())
            LOG_WARNING(TagLink, "no receive-side logging: could not start its task");

        const esp_err_t err = esp_now_init();
        if (err != ESP_OK)
        {
            LOG_ERROR(TagLink, "esp_now_init failed: %s", esp_err_to_name(err));
            return false;
        }
        esp_now_register_recv_cb(receiveCallback);
        esp_now_register_send_cb(sendCallback);

        // The station default is modem sleep: once associated, the radio only wakes around the
        // AP's beacons, so the gateway's ESP-NOW beacons and ACKs mostly arrive while it is off.
        const esp_err_t ps = esp_wifi_set_ps(WIFI_PS_NONE);
        if (ps != ESP_OK)
            LOG_WARNING(TagLink, "could not disable Wi-Fi power save: %s", esp_err_to_name(ps));

        esp_now_peer_info_t broadcast = {};
        memcpy(broadcast.peer_addr, BroadcastMac, 6);
        broadcast.ifidx = WIFI_IF_STA;
        broadcast.encrypt = false;
        const esp_err_t peer = esp_now_add_peer(&broadcast);
        if (peer != ESP_OK && peer != ESP_ERR_ESPNOW_EXIST)
            LOG_ERROR(TagLink, "adding the broadcast peer failed: %s", esp_err_to_name(peer));

        portENTER_CRITICAL(&lock);
        gatewayKnown = false;
        missed = 0;
        needResync = false;
        lastRttMs = 0;
        portEXIT_CRITICAL(&lock);
        reassembly = Reassembly();

        running = true;
        taskExited = false;
        // 4 KB, not 3: NMLog::write formats into a 256-byte stack buffer and then
        // Serial.printf formats again, and this task can log from inside sendRaw.
        if (xTaskCreate(clientTask, "espnow_client", 4096, nullptr, 1, &task) != pdPASS)
        {
            LOG_ERROR(TagLink, "could not create the client task");
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
