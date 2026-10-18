#pragma once
// NightMare Gateway: an ESP-NOW broker for NightMare devices, bridged to an
// MQTT broker, that is itself a NightMare device. Compiled in with
// NM_ENABLE_GATEWAY; see NightMare/Features.h for its flags.
//
//   void setup()
//   {
//       NightMareGatewayConfig config;
//       config.espnowConfig.psk = ...;
//       NightMareGateway_init(config);   // instead of startNightMareESP()
//   }
//
//   void loop()
//   {
//       NightMareGateway_tick();         // instead of tickNightMareESP()
//   }
//
// The gateway owns NightMare's lifecycle: whoever ticks it also ticks
// NightMare, which is what keeps Resources, Configs and the device table on
// one task.

#include <NightMare/Features.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <Gateway/Broker/Message.h>

struct NightMareMqttBrokerConfig 
{
    uint8_t maxClients; //not used do not touch
};

struct NightMareEspNowConfig
{
    // The ESP-NOW network key, shared with every device (16..64 bytes).
    // NightMareGateway_init() copies it: the caller's buffer may be wiped
    // or freed once that returns.
    const uint8_t *psk = nullptr;
    size_t pskLength = 0;
};

// Which broker the uplink reaches, as NightMare devices read it in the gateway
// announcement (uplinks.remote_mqtt / uplinks.local_mqtt).
enum class NightMareGatewayUplink : uint8_t
{
    REMOTE,
    LOCAL,
};

// NightMareGateway_init() copies every string: the caller's may go away.
struct NightMareMqttConfig
{
    NightMareGatewayUplink uplink = NightMareGatewayUplink::LOCAL;
    const char *host = nullptr; // without a scheme: "mqtt.example.com"
    uint16_t port = 1883;
    // TLS also makes the uplink wait for a valid clock: the broker
    // certificate's dates are checked.
    bool tls = false;
    const char *caCertificate = nullptr; // PEM, for tls
    const char *username = nullptr;
    const char *password = nullptr;
};

// Who runs the gateway, like the Scheduler's run mode.
enum class NightMareGatewayTickMode : uint8_t
{
    TASK,   // its own FreeRTOS task, woken by ESP-NOW and MQTT arrivals
    MANUAL, // only NightMareGateway_tick(), from the application's loop
};

// What the gateway is connected to, for a status indicator.
enum class NightMareGatewayLink : uint8_t
{
    NONE,
    LOCAL,   // ESP-NOW devices subscribed, no MQTT uplink
    REMOTE,  // MQTT uplink, no ESP-NOW devices
    BRIDGED, // both
};

struct NightMareGatewayConfig
{
    // Each side also needs its NM_GATEWAY_* flag compiled in.
    bool espnowBroker = NM_GATEWAY_ESPNOW;
    bool mqttClient = NM_GATEWAY_MQTT;
    bool mqttBroker = true; // not implemented yet do not implent.
    NightMareEspNowConfig espnowConfig;
    NightMareMqttConfig mqttConfig;
    // Wi-Fi belongs to the application; the gateway only asks about it.
    // Required: whether the Wi-Fi driver is started, which is all ESP-NOW needs.
    bool (*radioReady)() = nullptr;
    // Required with mqttClient: whether the station has an IP address.
    bool (*ipReady)() = nullptr;
    NightMareGatewayTickMode tickMode =
        NM_GATEWAY_OWN_TASK ? NightMareGatewayTickMode::TASK : NightMareGatewayTickMode::MANUAL;
    // Application work that touches NightMare Resources (reading a sensor into
    // a ManagedSensor, ...): run by every gateway tick, on the task that owns
    // them, whether or not the gateway is on.
    void (*onTick)() = nullptr;
    // Called from the gateway tick when the link changes, and once at start.
    void (*onLinkChange)(NightMareGatewayLink link) = nullptr;
};

enum class NightMareGatewayState : uint8_t
{
    Stopped,
    WaitForWifiStack,
    StartEspNow,
    Running,
};

// The application owns the config source (creds.h, provisioning, ...); the
// gateway keeps what it needs from `config` and not the struct itself.
// Bind the application's Resources first: this runs startNightMareESP(), on
// the gateway task in TASK mode or on the caller in MANUAL mode. False when the
// config is unusable (espnowBroker without a valid PSK, mqttClient without a
// host or ipReady, no radioReady, a side that is not compiled in), the task
// cannot start, or it was already initialized.
bool NightMareGateway_init(const NightMareGatewayConfig &config);

// One pass of the gateway and of NightMare (tickNightMareESP()). MANUAL mode
// only: in TASK mode the gateway's task does this and the call returns at once,
// so a loop can call it either way. Call it often: ESP-NOW and MQTT arrivals
// wait for it, and the beacon and session pruning are timed off it.
void NightMareGateway_tick();

// For the gateway's own drivers: work has arrived. Wakes the gateway task in
// TASK mode; in MANUAL mode the next NightMareGateway_tick() picks it up.
void NightMareGateway_wake();

// Thread-safe views of the retained-message vault, for the web UI.
size_t NightMareGateway_vaultSnapshot(std::vector<NightMare::RetainedEntry> &out, uint32_t *version = nullptr);
// Changes whenever the vault does; see MessageVault::version().
uint32_t NightMareGateway_vaultVersion();
size_t NightMareGateway_vaultSize();

// "gateway off": stops ESP-NOW (sessions dropped, no beacon) and MQTT. "on"
// starts them again; devices have to reconnect. Asynchronous: the gateway tick
// does the work, watch NightMareGateway_state() for Stopped / Running.
void NightMareGateway_enable(bool enabled);
bool NightMareGateway_enabled();

// Diagnostic: "espnow off" tears ESP-NOW down (sessions dropped, no beacon)
// while MQTT keeps running; "on" brings it back and devices reconnect. Lets a
// flaky MQTT link be tested with the radio shared by nothing else. Cleared by
// "gateway off". Asynchronous, like NightMareGateway_enable().
void NightMareGateway_pauseEspNow(bool paused);
bool NightMareGateway_espNowPaused();

NightMareGatewayState NightMareGateway_state();
const char *NightMareGateway_stateName(NightMareGatewayState state);

// Publishes a gateway-originated message to everyone: the MQTT broker (when
// connected) and every connected ESP-NOW device that has at least one
// subscription filter matching the topic. Retains it in the vault if flagged
// persistent. Must be called from the gateway tick (it walks the device
// table). True if it reached at least one side (a local send, or queued for MQTT).
bool publishToAll(const NightMare::Message &message);

// Answers a "<x>/in" request on "<x>/out" -- the same convention a
// NightMareNetwork device's own console uses -- reaching whoever is
// subscribed to the reply topic, locally and/or over MQTT. Exposed for any
// future gateway/... command handler to reuse, not just the debug one.
// By default the answer goes back only to the side `current` came from (an
// ESP-NOW device -> local subscribers; MQTT -> the broker); Both publishes it
// everywhere like publishToAll().
enum class ReplyTarget : uint8_t
{
    Source,
    Both,
};
void replyMessage(const NightMare::Message &current, const NightMare::Message &response,
                  ReplyTarget target = ReplyTarget::Source);

// Same delivery rule as replyMessage, but `response` keeps the topic it already
// has (no "/in" -> "/out" rewrite).
bool respondTo(const NightMare::Message &current, const NightMare::Message &response,
               ReplyTarget target = ReplyTarget::Source);
