#pragma once
#include <NightMare/Features.h>
#if NM_NETWORK_ESPNOW

// ESP-IDF only. Client for the Nightmare Gateway's ESP-NOW protocol; the wire
// contract is in docs/modules/espnow-protocol.md.
//
// Connecting is a handshake, not a guess: CONNECT -> CHALLENGE -> AUTH ->
// CONNACK proves both ends hold the network PSK (NM_ESPNOW_PSK in creds.h),
// the gateway assigns a session id (cid), both ends derive a per-session key
// and turn on ESP-NOW encryption, and an encrypted PING/PONG confirms it.
// Only then is the client CONNECTED. Subscriptions and the last will are kept
// here and re-sent automatically after every new session; the application
// never resyncs anything itself.
//
// Requirements: the Wi-Fi radio is running (Network/WiFiRadio) -- an AP and IP
// are not needed. With no AP configured the client hops channels until a
// gateway answers; with one configured it stays on the AP's channel, so the
// gateway must share that AP.
#include <cstddef>
#include <cstdint>

namespace NightMare::EspNowClient
{
enum class State : uint8_t
{
    STOPPED,
    SEARCHING,      // no gateway yet: probing (and hopping channels if free to)
    CONNECTING,     // CONNECT sent to a known gateway, waiting for CHALLENGE
    AUTHENTICATING, // AUTH sent, waiting for CONNACK
    SECURING,       // LMK installed, encrypted PING sent, waiting for PONG
    CONNECTED       // secure session: publish/subscribe traffic flows
};

const char *stateName(State state);

struct Settings
{
    // Unanswered heartbeats before the session is declared lost. The interval
    // itself is the gateway's, from CONNACK.
    uint8_t missedBeforeLost = 3;
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
// Sends DISCONNECT first when connected, so the gateway drops the session
// without firing the last will.
void end();
State state();
// Round trip of the last answered heartbeat, 0 until one has been answered.
uint32_t rttMs();
// The current session id, 0 when not in a session.
uint16_t sessionId();

void onState(StateCallback callback);
void onMessage(MessageCallback callback);

// Kept and re-sent after every new session; also sent right away when CONNECTED.
bool subscribe(const char *filter);
bool unsubscribe(const char *filter);
bool setLastWill(const char *topic, const uint8_t *payload, size_t length,
                 bool retained = false);

// Only while CONNECTED. A message may span up to 16 frames (~3.8 KB).
bool publish(const char *topic, const uint8_t *payload, size_t length,
             bool retained = false);
}

#endif // NM_NETWORK_ESPNOW
