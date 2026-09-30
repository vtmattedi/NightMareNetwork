#pragma once
#include <NightMare/Features.h>
#if NM_NETWORK_ESPNOW

// ESP-IDF only. Lean client for the Nightmare Gateway's ESP-NOW protocol.
//
// The gateway registers any device it hears, drops one that stays silent for
// 300 s, and answers a keep-alive (CONTROL, no data) with an ACK. This client
// finds the gateway from its periodic beacon (or from the ACK to a broadcast
// probe), keeps the registration alive with an automatic heartbeat, and
// re-sends its subscriptions and last will whenever it (re)connects.
//
// Requirements: esp_wifi is initialised and started in STA mode, and this node
// is on the gateway's channel (e.g. associated to the same AP).
#include <cstddef>
#include <cstdint>

namespace NightMare::EspNowClient
{
enum class State : uint8_t
{
    STOPPED,
    SEARCHING, // no gateway has answered yet, or it went silent
    CONNECTED  // the gateway answered within the last few heartbeats
};

struct Settings
{
    uint32_t heartbeatMs = 30000; // must stay well under the gateway's 300 s timeout
    uint8_t missedBeforeLost = 3; // unanswered heartbeats before CONNECTED -> SEARCHING
    // Radio channel to search on while the station is NOT associated to an AP
    // (an associated station is pinned to its AP's channel and is left alone).
    // 0 hops through channels 1..13 until a gateway answers, then stays on it.
    uint8_t channel = 0;
};

using StateCallback = void (*)(State state);
// Runs on the ESP-NOW receive task: keep it short and do not block.
using MessageCallback = void (*)(const char *topic, const uint8_t *payload,
                                 size_t length, bool retained);

bool begin(const Settings &settings = Settings());
void end();
State state();
// Round trip of the last answered heartbeat, 0 until one has been answered.
uint32_t rttMs();

void onState(StateCallback callback);
void onMessage(MessageCallback callback);

// Kept and re-sent after every reconnect; also sent right away when CONNECTED.
bool subscribe(const char *filter);
bool unsubscribe(const char *filter);
bool setLastWill(const char *topic, const uint8_t *payload, size_t length,
                 bool retained = false);

// Only while CONNECTED. A message may span up to 16 frames (~3.8 KB).
bool publish(const char *topic, const uint8_t *payload, size_t length,
             bool retained = false);
}

#endif // NM_NETWORK_ESPNOW
