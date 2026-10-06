#include <NightMare/Features.h>
#if NM_NETWORK_ESPNOW

#include "EspNowClient.h"
#include "NightMareEspNow/Auth.h"
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

#if __has_include(<creds.h>)
#include <creds.h>
#endif
#ifndef NM_ESPNOW_PSK
#error "NM_NETWORK_ESPNOW needs NM_ESPNOW_PSK (the gateway's ESP-NOW network key) defined in creds.h"
#endif
static_assert(sizeof(NM_ESPNOW_PSK) - 1 >= NightMare::EspNowAuth::MinPskLength,
              "NM_ESPNOW_PSK must be at least 16 characters");

namespace NightMare::EspNowClient
{
    namespace
    {
        namespace Auth = NightMare::EspNowAuth;

        // NightMare's LOG, not ESP_LOGx: the Arduino core hides ESP_LOG output,
        // which is how every failure in here used to vanish.
        constexpr char TagLink[] = "ESPNOW";  // search, handshake, session state
        constexpr char TagTx[] = "ESPNOW-TX"; // outgoing frames
        constexpr char TagRx[] = "ESPNOW-RX"; // incoming frames
        constexpr size_t MaxTopicLength = 63; // 6-bit length in the message encoding
        constexpr uint32_t SearchIntervalMs = 3000;   // probe rate on a fixed/AP channel
        constexpr uint32_t HopIntervalMs = 300;       // dwell per channel while hopping
        constexpr uint32_t HandshakeStepMs = 1000;    // CONNECT->CHALLENGE, AUTH->CONNACK
        constexpr uint32_t SecuringRetryMs = 300;     // between encrypted PINGs while SECURING
        constexpr uint8_t SecuringPings = 5;          // then the handshake failed
        constexpr uint8_t HandshakeAttempts = 3;      // on one gateway before searching again
        constexpr uint32_t RejectedBackoffMs = 10000; // after AUTH_FAILED / UNSUPPORTED_VERSION
        constexpr uint32_t MinHeartbeatMs = 1000;
        constexpr uint32_t MaxHeartbeatMs = 120000;
        constexpr uint8_t LastHopChannel = 13;
        constexpr uint32_t SendTimeoutMs = 50;
        constexpr uint32_t ReassemblyTimeoutMs = 5000;
        constexpr uint8_t BroadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        constexpr uint8_t Psk[] = NM_ESPNOW_PSK;
        constexpr size_t PskLength = sizeof(Psk) - 1; // not the terminator

        // `lock` (spinlock, tiny critical sections only) guards what the API and
        // the receive callback read: state, gateway, cid, rtt, message ids, callbacks.
        // `willMutex` guards the last will; `sendMutex` serialises
        // transmissions, since ESP-NOW allows one in flight.
        // Everything else about the handshake belongs to the client task alone.
        portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
        SemaphoreHandle_t willMutex = nullptr;
        SemaphoreHandle_t sendMutex = nullptr;
        SemaphoreHandle_t sendDone = nullptr;
        TaskHandle_t task = nullptr;
        volatile bool running = false;
        volatile bool taskExited = true;

        Settings settings;
        State currentState = State::STOPPED;
        bool gatewayKnown = false;
        uint8_t gatewayMac[6] = {};
        char selectedGatewayId[65] = {};
        uint16_t cid = 0;
        uint32_t lastRttMs = 0;
        uint16_t nextMessageId = 1;
        StateCallback stateCallback = nullptr;
        MessageCallback messageCallback = nullptr;

        bool willSet = false;
        std::vector<uint8_t> willRaw;

        // Client-task only.
        struct Handshake
        {
            uint8_t selfMac[6] = {};
            uint64_t clientNonce = 0;
            uint64_t gatewayNonce = 0;
            uint16_t connectId = 0; // CHALLENGE echoes it
            uint16_t authId = 0;    // CONNACK echoes it
            uint16_t pingId = 0;    // PONG echoes it
            uint64_t pingSentUs = 0;
            uint8_t pingTries = 0;
            uint8_t missed = 0;
            uint8_t failures = 0;
            uint32_t heartbeatMs = 0;
            bool peerEncrypted = false;
            uint64_t deadlineUs = 0;
            uint64_t backoffUntilUs = 0;
        } hs;

        // Keyed by cid + messageId: a new session may reuse a messageId, and its
        // fragments must never join a message left over from the old one.
        struct Reassembly
        {
            bool active = false;
            uint16_t cid = 0;
            uint16_t messageId = 0;
            uint8_t nextFrame = 0;
            uint8_t totalFrames = 0;
            uint64_t startedUs = 0;
            std::vector<uint8_t> buffer;
        } reassembly; // receive callback only

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

        // --- Receive-side warnings ---------------------------------------------
        // The receive callback runs on the Wi-Fi task, which has no stack to spare for
        // formatting a log line (NMLog + Serial.printf, the same thing that overflowed
        // sys_evt). It only queues plain data here; rxLogTask formats and prints it.
        enum class RxNote : uint8_t
        {
            MessageIgnored, // value: 1 = not our gateway, 0 = not our session
            QueueFull,
        };

        struct RxEvent
        {
            RxNote note;
            uint8_t type;
            int8_t rssi;
            uint16_t messageId;
            uint16_t cid;
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
                event.messageId = header->messageId;
                event.cid = header->cid;
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
            case RxNote::MessageIgnored:
                LOG_WARNING(TagRx, "MESSAGE #%u from %s ignored: %s", e.messageId, from.text,
                            e.value != 0 ? "not our gateway" : "not our session");
                break;
            case RxNote::QueueFull:
                LOG_WARNING(TagRx, "%s #%u from %s dropped: client queue full", type, e.messageId,
                            from.text);
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

        // Handshake and heartbeat replies, handed from the receive callback to the
        // client task: answering them means sending, and a send blocks on the send
        // callback, which runs on the very task the receive callback does.
        struct RxFrame
        {
            uint8_t mac[6];
            Frame frame;
        };
        constexpr UBaseType_t ControlDepth = 8;
        QueueHandle_t controlQueue = nullptr;

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
            const bool needed = willMutex == nullptr || sendMutex == nullptr || sendDone == nullptr;
            portEXIT_CRITICAL(&lock);
            if (!needed)
                return true;
            // Only called from begin()/the will setter before concurrent use.
            if (willMutex == nullptr)
                willMutex = xSemaphoreCreateMutex();
            if (sendMutex == nullptr)
                sendMutex = xSemaphoreCreateMutex();
            if (sendDone == nullptr)
                sendDone = xSemaphoreCreateBinary();
            return willMutex != nullptr && sendMutex != nullptr && sendDone != nullptr;
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

        uint16_t currentCid()
        {
            portENTER_CRITICAL(&lock);
            const uint16_t value = cid;
            portEXIT_CRITICAL(&lock);
            return value;
        }

        void sendCallback(const esp_now_send_info_t *, esp_now_send_status_t status)
        {
            lastSendStatus = status;
            if (sendDone != nullptr)
                xSemaphoreGive(sendDone);
        }

        // Whether it goes out encrypted is the peer's setting, not the frame's.
        bool sendRaw(const uint8_t *mac, const Frame &frame)
        {
            Guard guard(sendMutex);
            xSemaphoreTake(sendDone, 0); // drop a stale completion from a timed-out send
            const esp_err_t err = esp_now_send(mac, reinterpret_cast<const uint8_t *>(&frame),
                                               frameSize(frame));
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

        bool sendTo(const uint8_t *mac, FrameType type, uint16_t messageId, uint16_t frameCid,
                    const void *data = nullptr, size_t length = 0)
        {
            Frame frame{};
            if (!makeFrame(frame, type, messageId, frameCid, data, length))
            {
                LOG_ERROR(TagTx, "%s: %u data bytes exceed a frame", frameTypeName(static_cast<uint8_t>(type)),
                          static_cast<unsigned>(length));
                return false;
            }
            return sendRaw(mac, frame);
        }

        // Session traffic to the gateway: needs a session, carries its cid.
        bool sendSession(FrameType type, const uint8_t *data, size_t length)
        {
            uint8_t mac[6];
            const uint16_t session = currentCid();
            if (session == 0 || !gatewayAddress(mac))
                return false;
            return sendTo(mac, type, newMessageId(), session, data, length);
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
            const uint16_t session = currentCid();
            if (session == 0 || !gatewayAddress(mac))
                return false;
            const size_t totalFrames = (raw.size() + MaxFrameDataSize - 1) / MaxFrameDataSize;
            if (totalFrames == 0 || totalFrames > UINT8_MAX) // the header's fragment fields
                return false;
            const uint16_t id = newMessageId();
            for (size_t i = 0; i < totalFrames; ++i)
            {
                const size_t offset = i * MaxFrameDataSize;
                const size_t chunk = raw.size() - offset < MaxFrameDataSize ? raw.size() - offset
                                                                            : MaxFrameDataSize;
                Frame frame{};
                if (!makeFrame(frame, FrameType::MESSAGE, id, session, raw.data() + offset, chunk,
                               static_cast<uint8_t>(i), static_cast<uint8_t>(totalFrames)) ||
                    !sendRaw(mac, frame))
                    return false;
            }
            return true;
        }

        // --- Peer ---------------------------------------------------------------

        esp_now_peer_info_t gatewayPeer(const uint8_t *mac)
        {
            esp_now_peer_info_t peer = {};
            memcpy(peer.peer_addr, mac, 6);
            peer.channel = 0; // follow the station's channel
            peer.ifidx = WIFI_IF_STA;
            peer.encrypt = false;
            return peer;
        }

        bool setGateway(const uint8_t *mac)
        {
            esp_now_peer_info_t peer = gatewayPeer(mac);
            esp_err_t err = esp_now_add_peer(&peer);
            if (err == ESP_ERR_ESPNOW_EXIST)
                err = esp_now_mod_peer(&peer); // back to plaintext for the handshake
            if (err != ESP_OK)
            {
                LOG_ERROR(TagLink, "add gateway peer %s failed: %s", macText(mac).text,
                          esp_err_to_name(err));
                return false;
            }
            portENTER_CRITICAL(&lock);
            memcpy(gatewayMac, mac, 6);
            gatewayKnown = true;
            portEXIT_CRITICAL(&lock);
            hs.peerEncrypted = false;
            LOG(TagLink, "gateway %s on ch %u", macText(mac).text, currentChannel());
            return true;
        }

        void setGatewayIdentity(const Frame &beacon)
        {
            char id[sizeof(selectedGatewayId)] = {};
            beaconGatewayId(beacon, id, sizeof(id));
            portENTER_CRITICAL(&lock);
            memcpy(selectedGatewayId, id, sizeof(selectedGatewayId));
            portEXIT_CRITICAL(&lock);
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
            selectedGatewayId[0] = '\0';
            portEXIT_CRITICAL(&lock);
            hs.peerEncrypted = false;
        }

        bool installLmk(const uint8_t *lmk)
        {
            uint8_t mac[6];
            if (!gatewayAddress(mac))
                return false;
            esp_now_peer_info_t peer = gatewayPeer(mac);
            peer.encrypt = true;
            memcpy(peer.lmk, lmk, ESP_NOW_KEY_LEN);
            const esp_err_t err = esp_now_mod_peer(&peer);
            Auth::wipe(peer.lmk, sizeof(peer.lmk));
            if (err != ESP_OK)
            {
                LOG_ERROR(TagLink, "installing the session key failed: %s", esp_err_to_name(err));
                return false;
            }
            hs.peerEncrypted = true;
            return true;
        }

        // Back to "no session": cid 0 and a plaintext peer, ready for a new handshake.
        void dropSession()
        {
            portENTER_CRITICAL(&lock);
            cid = 0;
            portEXIT_CRITICAL(&lock);
            reassembly.active = false;
            Auth::wipe(&hs.clientNonce, sizeof(hs.clientNonce));
            Auth::wipe(&hs.gatewayNonce, sizeof(hs.gatewayNonce));
            uint8_t mac[6];
            if (hs.peerEncrypted && gatewayAddress(mac))
            {
                esp_now_peer_info_t peer = gatewayPeer(mac);
                if (esp_now_mod_peer(&peer) != ESP_OK)
                    LOG_WARNING(TagLink, "could not turn the gateway peer back to plaintext");
            }
            hs.peerEncrypted = false;
        }

        // --- Channel search -----------------------------------------------------

        // True only when no IP station owns the channel, so the client may pick it.
        // An associated station is pinned to its AP's channel. One with an AP configured but not
        // associated yet is mid-handshake (or retrying): retuning the radio under it every hop
        // breaks the join, the station retries, the next hop breaks that too, and neither ever
        // finishes. Leave the channel to the AP in both cases. With the network layer built this
        // does not arise: NmConnection suspends the station (WiFi_stop() clears its config)
        // whenever ESP-NOW is selected, so the search hops every channel. A radio-only device
        // has no station config at all and hops too.
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
                LOG_DEBUG(TagLink, "searching on ch %u", channel);
        }

        void wakeTask()
        {
            if (task != nullptr)
                xTaskNotifyGive(task);
        }

        // --- Handshake (client task) ------------------------------------------

        void setDeadline(uint32_t fromNowMs)
        {
            hs.deadlineUs = nowUs() + static_cast<uint64_t>(fromNowMs) * 1000;
        }

        // Broadcast even to a known gateway: a CONNECT has to get through
        // whatever state the gateway still holds for us, including an encrypted
        // peer from a session we lost (e.g. we rebooted). A broadcast also finds
        // a gateway we do not know yet: its CHALLENGE answers with its address.
        void sendConnect()
        {
            hs.clientNonce = Auth::freshNonce();
            hs.connectId = newMessageId();
            const ConnectPayload connect{hs.clientNonce, 0};
            sendTo(BroadcastMac, FrameType::CONNECT, hs.connectId, 0, &connect, sizeof(connect));
        }

        void startHandshake()
        {
            dropSession();
            setState(State::CONNECTING);
            sendConnect();
            setDeadline(HandshakeStepMs);
        }

        void enterSearching(uint32_t backoffMs = 0)
        {
            dropSession();
            forgetGateway();
            hs.failures = 0;
            setState(State::SEARCHING);
            hs.backoffUntilUs = nowUs() + static_cast<uint64_t>(backoffMs) * 1000;
            hs.deadlineUs = hs.backoffUntilUs;
        }

        void handshakeFailed(const char *why)
        {
            ++hs.failures;
            LOG_WARNING(TagLink, "handshake failed (%s), attempt %u of %u", why, hs.failures,
                        HandshakeAttempts);
            if (hs.failures >= HandshakeAttempts)
                enterSearching();
            else
                startHandshake();
        }

        void sendPing()
        {
            uint8_t mac[6];
            const uint16_t session = currentCid();
            if (session == 0 || !gatewayAddress(mac))
                return;
            hs.pingId = newMessageId();
            hs.pingSentUs = nowUs();
            sendTo(mac, FrameType::PING, hs.pingId, session);
        }

        void restoreLastWill()
        {
            std::vector<uint8_t> will;
            {
                Guard guard(willMutex);
                if (willSet)
                    will = willRaw;
            }
            if (!will.empty() && !sendSession(FrameType::LAST_WILL, will.data(), will.size()))
                LOG_WARNING(TagLink, "could not restore LAST_WILL");
        }

        Auth::HandshakeContext handshakeContext(const uint8_t *gateway)
        {
            Auth::HandshakeContext context{};
            memcpy(context.clientMac, hs.selfMac, 6);
            memcpy(context.gatewayMac, gateway, 6);
            context.clientNonce = hs.clientNonce;
            context.gatewayNonce = hs.gatewayNonce;
            return context;
        }

        void onChallenge(const uint8_t *from, const Frame &frame)
        {
            const State now = state();
            if ((now != State::SEARCHING && now != State::CONNECTING) ||
                frame.header.messageId != hs.connectId)
                return; // a stale CHALLENGE, or one for another client's CONNECT

            uint8_t known[6];
            if (gatewayAddress(known))
            {
                if (memcmp(known, from, 6) != 0)
                    return; // another gateway answered our broadcast; we chose this one
            }
            else if (!setGateway(from))
            {
                return;
            }

            ChallengePayload challenge;
            memcpy(&challenge, frame.data, sizeof(challenge));
            hs.gatewayNonce = challenge.gatewayNonce;

            Auth::HandshakeContext context = handshakeContext(from);
            AuthPayload auth{};
            const bool ok = Auth::authProof(Psk, PskLength, context, auth.proof);
            Auth::wipe(&context, sizeof(context));
            if (!ok)
            {
                LOG_ERROR(TagLink, "could not compute the AUTH proof");
                handshakeFailed("no proof");
                return;
            }
            hs.authId = newMessageId();
            setState(State::AUTHENTICATING);
            sendTo(from, FrameType::AUTH, hs.authId, 0, &auth, sizeof(auth));
            setDeadline(HandshakeStepMs);
        }

        void onConnAck(const Frame &frame)
        {
            if (state() != State::AUTHENTICATING || frame.header.messageId != hs.authId)
                return;

            ConnAckPayload ack;
            memcpy(&ack, frame.data, sizeof(ack));
            uint32_t heartbeat = ack.heartbeatMs;
            if (heartbeat < MinHeartbeatMs)
                heartbeat = MinHeartbeatMs;
            if (heartbeat > MaxHeartbeatMs)
                heartbeat = MaxHeartbeatMs;
            hs.heartbeatMs = heartbeat;

            uint8_t mac[6];
            if (!gatewayAddress(mac))
                return;
            Auth::HandshakeContext context = handshakeContext(mac);
            uint8_t lmk[Auth::LmkSize];
            const bool derived = Auth::sessionLmk(Psk, PskLength, context, lmk);
            Auth::wipe(&context, sizeof(context));
            Auth::wipe(&hs.clientNonce, sizeof(hs.clientNonce));
            Auth::wipe(&hs.gatewayNonce, sizeof(hs.gatewayNonce));
            const bool installed = derived && installLmk(lmk);
            Auth::wipe(lmk, sizeof(lmk));
            if (!installed)
            {
                handshakeFailed("session key");
                return;
            }

            portENTER_CRITICAL(&lock);
            cid = frame.header.cid;
            portEXIT_CRITICAL(&lock);
            LOG(TagLink, "authenticated as session %u, securing", frame.header.cid);
            setState(State::SECURING);
            hs.pingTries = 1;
            sendPing();
            setDeadline(SecuringRetryMs);
        }

        void onPong(const Frame &frame)
        {
            if (frame.header.cid != currentCid() || frame.header.messageId != hs.pingId)
                return;
            const uint32_t rtt = static_cast<uint32_t>((nowUs() - hs.pingSentUs) / 1000);
            portENTER_CRITICAL(&lock);
            lastRttMs = rtt;
            portEXIT_CRITICAL(&lock);
            hs.missed = 0;
            if (state() != State::SECURING)
                return;

            // The PONG came back through the session key: both ends installed the same LMK.
            hs.failures = 0;
            uint8_t mac[6];
            gatewayAddress(mac);
            LOG(TagLink, "connected to gateway %s, session %u, rtt %lu ms, heartbeat %lu ms",
                macText(mac).text, frame.header.cid, static_cast<unsigned long>(rtt),
                static_cast<unsigned long>(hs.heartbeatMs));
            restoreLastWill();
            setState(State::CONNECTED);
            setDeadline(hs.heartbeatMs);
        }

        void onError(const Frame &frame)
        {
            const ErrorCode code = static_cast<ErrorCode>(frame.data[0]);
            const State now = state();
            switch (code)
            {
            case ErrorCode::AUTH_FAILED:
                if (now != State::AUTHENTICATING)
                    return;
                LOG_ERROR(TagLink, "gateway rejected our AUTH: NM_ESPNOW_PSK differs from the gateway's");
                enterSearching(RejectedBackoffMs);
                return;
            case ErrorCode::UNSUPPORTED_VERSION:
                LOG_ERROR(TagLink, "gateway does not speak NM protocol %u", NM_PROTOCOL_VERSION);
                enterSearching(RejectedBackoffMs);
                return;
            case ErrorCode::INVALID_SESSION:
            case ErrorCode::NOT_CONNECTED:
                if (now != State::SECURING && now != State::CONNECTED)
                    return;
                LOG_WARNING(TagLink, "gateway no longer knows session %u, reconnecting", currentCid());
                if (now == State::CONNECTED)
                    setState(State::CONNECTING); // leaves CONNECTED before the new handshake
                startHandshake();
                return;
            case ErrorCode::REJECTED:
                LOG_WARNING(TagLink, "gateway refused request #%u (subscription table full?)",
                            frame.header.messageId);
                return;
            case ErrorCode::INVALID_FRAME:
                LOG_WARNING(TagLink, "gateway reports an invalid frame from us");
                return;
            }
        }

        void handleControl(const RxFrame &rx)
        {
            uint8_t known[6];
            const bool haveGateway = gatewayAddress(known);
            const bool fromGateway = haveGateway && memcmp(known, rx.mac, 6) == 0;
            switch (static_cast<FrameType>(rx.frame.header.type))
            {
            case FrameType::BEACON:
                if (state() != State::SEARCHING || haveGateway || nowUs() < hs.backoffUntilUs)
                    return;
                if (!beaconSupports(rx.frame, EspNowFrameVersion::V1))
                {
                    LOG_WARNING(TagRx, "BEACON from %s does not offer V1 framing, ignored",
                                macText(rx.mac).text);
                    return;
                }
                LOG(TagRx, "BEACON from %s, connecting", macText(rx.mac).text);
                if (setGateway(rx.mac))
                {
                    setGatewayIdentity(rx.frame);
                    startHandshake();
                }
                return;
            case FrameType::CHALLENGE:
                onChallenge(rx.mac, rx.frame);
                return;
            case FrameType::CONNACK:
                if (fromGateway)
                    onConnAck(rx.frame);
                return;
            case FrameType::PONG:
                if (fromGateway)
                    onPong(rx.frame);
                return;
            case FrameType::ERROR:
                if (fromGateway)
                    onError(rx.frame);
                return;
            default:
                return;
            }
        }

        // Due work for the current state, once its deadline has passed.
        void runTimers()
        {
            const uint64_t now = nowUs();
            if (now < hs.deadlineUs)
                return;
            switch (state())
            {
            case State::SEARCHING:
            {
                if (now < hs.backoffUntilUs)
                {
                    hs.deadlineUs = hs.backoffUntilUs;
                    return;
                }
                const bool hopping = hoppingNow();
                if (hopping)
                    tuneForSearch(); // tune first, probe second, then a full dwell for the answer
                sendConnect();
                setDeadline(hopping ? HopIntervalMs : SearchIntervalMs);
                return;
            }
            case State::CONNECTING:
                handshakeFailed("no CHALLENGE");
                return;
            case State::AUTHENTICATING:
                handshakeFailed("no CONNACK");
                return;
            case State::SECURING:
                if (hs.pingTries >= SecuringPings)
                {
                    handshakeFailed("no PONG to the encrypted PING");
                    return;
                }
                ++hs.pingTries;
                sendPing();
                setDeadline(SecuringRetryMs);
                return;
            case State::CONNECTED:
                if (hs.missed >= settings.missedBeforeLost)
                {
                    LOG_WARNING(TagLink, "gateway silent for %u heartbeats, searching again",
                                settings.missedBeforeLost);
                    enterSearching();
                    return;
                }
                ++hs.missed;
                sendPing();
                setDeadline(hs.heartbeatMs);
                return;
            case State::STOPPED:
                return;
            }
        }

        // --- Receive callback (Wi-Fi task) --------------------------------------

        void handleMessageFrame(const Frame &frame)
        {
            const FrameHeader &h = frame.header;
            const uint8_t *data = frame.data;
            size_t length = h.length;
            std::vector<uint8_t> whole;
            if (h.totalFrames > 1)
            {
                Reassembly &r = reassembly;
                if (r.active && (nowUs() - r.startedUs) / 1000 > ReassemblyTimeoutMs)
                    r.active = false;
                if (!r.active || r.cid != h.cid || r.messageId != h.messageId)
                {
                    if (h.frameIndex != 0)
                        return; // joined mid-message
                    r.active = true;
                    r.cid = h.cid;
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
            // Runs on the Wi-Fi task: queue warnings with note(), never LOG -- see RxNote.
            const uint8_t *from = info->src_addr;
            const int rssi = info->rx_ctrl != nullptr ? info->rx_ctrl->rssi : 0;
            RxFrame rx;
            const FrameCheck check = decodeFrame(rx.frame, data, static_cast<size_t>(len));
            if (check != FrameCheck::OK)
                return;
            const FrameHeader &header = rx.frame.header;

            uint8_t gateway[6];
            const bool fromGateway = gatewayAddress(gateway) && memcmp(gateway, from, 6) == 0;

            switch (static_cast<FrameType>(header.type))
            {
            case FrameType::MESSAGE:
            {
                bool ours;
                portENTER_CRITICAL(&lock);
                ours = currentState == State::CONNECTED && header.cid == cid;
                portEXIT_CRITICAL(&lock);
                if (fromGateway && ours)
                    handleMessageFrame(rx.frame);
                else
                    note(RxNote::MessageIgnored, from, &header, rssi, fromGateway ? 0 : 1);
                return;
            }
            case FrameType::ACK:
                return;
            case FrameType::BEACON:
                // Only a searching client needs one; don't flood the queue otherwise.
                if (state() != State::SEARCHING)
                    return;
                break;
            case FrameType::CHALLENGE:
            case FrameType::CONNACK:
            case FrameType::PONG:
            case FrameType::ERROR:
                break;
            default:
                return;
            }

            memcpy(rx.mac, from, 6);
            if (controlQueue == nullptr || xQueueSend(controlQueue, &rx, 0) != pdTRUE)
            {
                note(RxNote::QueueFull, from, &header, rssi);
                return;
            }
            wakeTask();
        }

        void clientTask(void *)
        {
            // 0 = not searching, 1 = hopping channels, 2 = held on the AP's (or the fixed) channel
            uint8_t lastMode = 0xFF;
            while (running)
            {
                const uint64_t now = nowUs();
                const uint32_t waitMs = hs.deadlineUs > now
                                            ? static_cast<uint32_t>((hs.deadlineUs - now + 999) / 1000)
                                            : 0;
                if (waitMs > 0)
                    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(waitMs));
                if (!running)
                    break;

                RxFrame rx;
                while (xQueueReceive(controlQueue, &rx, 0) == pdTRUE)
                    handleControl(rx);
                runTimers();

                const bool searching = state() == State::SEARCHING;
                const uint8_t mode = !searching ? 0 : hoppingNow() ? 1 : 2;
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
            }
            taskExited = true;
            task = nullptr;
            vTaskDelete(nullptr);
        }
    }

    const char *stateName(State state)
    {
        switch (state)
        {
        case State::STOPPED: return "stopped";
        case State::SEARCHING: return "searching";
        case State::CONNECTING: return "connecting";
        case State::AUTHENTICATING: return "authenticating";
        case State::SECURING: return "securing";
        case State::CONNECTED: return "connected";
        }
        return "?";
    }

    bool begin(const Settings &config)
    {
        if (running)
            return true;
        if (config.missedBeforeLost == 0 || !ensureMutexes())
            return false;
        settings = config;

        if (controlQueue == nullptr)
            controlQueue = xQueueCreate(ControlDepth, sizeof(RxFrame));
        if (controlQueue == nullptr)
        {
            LOG_ERROR(TagLink, "could not allocate the control queue");
            return false;
        }
        xQueueReset(controlQueue);

        hs = Handshake();
        esp_wifi_get_mac(WIFI_IF_STA, hs.selfMac);
        wifi_config_t station = {};
        const bool haveApConfig = esp_wifi_get_config(WIFI_IF_STA, &station) == ESP_OK &&
                                  station.sta.ssid[0] != '\0';
        LOG(TagLink, "starting on %s, ch %u, protocol %u, lost after %u heartbeats, AP: %s",
            macText(hs.selfMac).text, currentChannel(), NM_PROTOCOL_VERSION, config.missedBeforeLost,
            haveApConfig ? reinterpret_cast<const char *>(station.sta.ssid) : "(none)");

        if (!ensureRxLog())
            LOG_WARNING(TagLink, "no receive-side logging: could not start its task");

        const esp_err_t err = esp_now_init();
        if (err != ESP_OK)
        {
            LOG_ERROR(TagLink, "esp_now_init failed: %s", esp_err_to_name(err));
            return false;
        }
        // The network's PMK, the same one the gateway derives from the PSK, not
        // Espressif's default. Set before any encrypted peer exists.
        uint8_t pmk[Auth::PmkSize];
        const bool pmkOk = Auth::networkPmk(Psk, PskLength, pmk) && esp_now_set_pmk(pmk) == ESP_OK;
        Auth::wipe(pmk, sizeof(pmk));
        if (!pmkOk)
        {
            LOG_ERROR(TagLink, "could not set the ESP-NOW PMK");
            esp_now_deinit();
            return false;
        }
        esp_now_register_recv_cb(receiveCallback);
        esp_now_register_send_cb(sendCallback);

        // The station default is modem sleep: once associated, the radio only wakes around the
        // AP's beacons, so the gateway's ESP-NOW beacons and replies mostly arrive while it is off.
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
        selectedGatewayId[0] = '\0';
        cid = 0;
        lastRttMs = 0;
        portEXIT_CRITICAL(&lock);
        reassembly = Reassembly();

        running = true;
        taskExited = false;
        setState(State::SEARCHING);
        // 6 KB: NMLog::write formats into a 256-byte stack buffer and then Serial.printf
        // formats again, sendRaw can log, and the HMAC runs on this task too.
        if (xTaskCreate(clientTask, "espnow_client", 6144, nullptr, 1, &task) != pdPASS)
        {
            LOG_ERROR(TagLink, "could not create the client task");
            running = false;
            taskExited = true;
            esp_now_unregister_recv_cb();
            esp_now_unregister_send_cb();
            esp_now_deinit();
            setState(State::STOPPED);
            return false;
        }
        return true;
    }

    void end()
    {
        if (!running)
            return;
        // A clean goodbye: the gateway ends the session without firing the last will.
        if (state() == State::CONNECTED)
            sendSession(FrameType::DISCONNECT, nullptr, 0);
        running = false;
        wakeTask();
        for (int i = 0; i < 100 && !taskExited; ++i)
            vTaskDelay(pdMS_TO_TICKS(10));
        esp_now_unregister_recv_cb();
        esp_now_unregister_send_cb();
        esp_now_deinit(); // drops every peer, the session key with them
        portENTER_CRITICAL(&lock);
        gatewayKnown = false;
        selectedGatewayId[0] = '\0';
        cid = 0;
        portEXIT_CRITICAL(&lock);
        Auth::wipe(&hs, sizeof(hs));
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

    uint16_t sessionId()
    {
        return currentCid();
    }

    String gatewayId()
    {
        char id[sizeof(selectedGatewayId)] = {};
        portENTER_CRITICAL(&lock);
        memcpy(id, selectedGatewayId, sizeof(id));
        portEXIT_CRITICAL(&lock);
        return String(id);
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
            state() != State::CONNECTED || !ensureMutexes())
            return false;
        return sendSession(FrameType::SUBSCRIBE,
                           reinterpret_cast<const uint8_t *>(filter), strlen(filter));
    }

    bool unsubscribe(const char *filter)
    {
        if (filter == nullptr || filter[0] == '\0' || strlen(filter) > MaxFrameDataSize ||
            state() != State::CONNECTED || !ensureMutexes())
            return false;
        return sendSession(FrameType::UNSUBSCRIBE,
                           reinterpret_cast<const uint8_t *>(filter), strlen(filter));
    }

    bool setLastWill(const char *topic, const uint8_t *payload, size_t length, bool retained)
    {
        std::vector<uint8_t> raw;
        if (!encodeMessage(raw, topic, payload, length, retained) ||
            raw.size() > MaxFrameDataSize || !ensureMutexes())
            return false;
        {
            Guard guard(willMutex);
            willRaw = raw;
            willSet = true;
        }
        if (state() == State::CONNECTED)
            sendSession(FrameType::LAST_WILL, raw.data(), raw.size());
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
