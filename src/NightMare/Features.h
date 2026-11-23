#pragma once
#include <Core/Logs.h>
// A consuming project may provide include/NightMareConfig.h. PlatformIO compiles
// every library .cpp; optional implementations guard their includes and code.
#if __has_include(<NightMareConfig.h>)
#include <NightMareConfig.h>
#endif

#ifndef NM_ENABLE_SETTINGS
#define NM_ENABLE_SETTINGS 1
#endif
#if !NM_ENABLE_SETTINGS
#error "Config<T> requires NM_ENABLE_SETTINGS because Config values are persistent"
#endif
#ifndef NM_ENABLE_RESOURCES
#define NM_ENABLE_RESOURCES 1
#endif
#ifndef NM_ENABLE_NETWORK
#define NM_ENABLE_NETWORK 1
#endif
#ifndef NM_ENABLE_CONSOLE
#define NM_ENABLE_CONSOLE 1
#endif
// The Wi-Fi IP station (Network/WiFiIP): join an AP and get an IP. Needed by
// MQTT, SNTP and OTA -- not by ESP-NOW, which only needs the radio.
#ifndef NM_ENABLE_WIFI
#define NM_ENABLE_WIFI 1
#endif
#ifndef NM_ENABLE_MQTT
#define NM_ENABLE_MQTT 1
#endif
// The MQTT broker profile (REMOTE or LOCAL) is chosen at runtime and saved
// (Mqtt_enable(profile), NETWORK MQTT ENABLE <profile>); a profile exists when
// creds.h describes its broker. The old compile-time profile switches are gone.
#if defined(NM_NETWORK_MQTT) || defined(NM_NETWORK_LOCALMQTT)
#error "NM_NETWORK_MQTT / NM_NETWORK_LOCALMQTT were removed: use NM_ENABLE_MQTT; the broker profile is a saved runtime setting (Mqtt_enable(profile)) and exists when creds.h describes that broker"
#endif
#ifndef NM_NETWORK_ESPNOW
#define NM_NETWORK_ESPNOW 0
#endif
// The Wi-Fi radio (Network/WiFiRadio): driver initialised and started in STA
// mode, with no association. Shared by the IP station, which joins an AP on
// top of it, and ESP-NOW, which needs nothing more. Derived, not a choice: it
// is on whenever something needs it.
#ifndef NM_ENABLE_WIFI_RADIO
#define NM_ENABLE_WIFI_RADIO (NM_ENABLE_WIFI || NM_NETWORK_ESPNOW)
#endif
#ifndef NM_ENABLE_TELEMETRY
#define NM_ENABLE_TELEMETRY 1
#endif
// Machine-facing framework documents are canonically MessagePack. Enable this
// only when a retained human-readable sibling is also required on the wire;
// commands and local diagnostic generation remain available either way.
#ifndef NM_ENABLE_JSON_WIRE
#define NM_ENABLE_JSON_WIRE 0
#endif
#ifndef NM_TELEMETRY_INTERVAL_MS
#define NM_TELEMETRY_INTERVAL_MS 60000UL
#endif
#ifndef NM_FIRMWARE_VERSION
#define NM_FIRMWARE_VERSION "unspecified"
#endif
#ifndef NM_ENABLE_SCHEDULER
#define NM_ENABLE_SCHEDULER 1
#endif
#ifndef NM_ENABLE_JOBS
#define NM_ENABLE_JOBS 1
#endif
#ifndef NM_ENABLE_TIME_SYNC
#define NM_ENABLE_TIME_SYNC 1
#endif
#ifndef NM_NAMESPACE
// Default namespace ID. Empty = the legacy unprefixed layout. See docs/architecture/namespaces.md.
#define NM_NAMESPACE ""
#endif
#ifndef NM_TOPIC_ROOT
#define NM_TOPIC_ROOT "nm"
#endif
#ifndef NM_TIMEZONE
#define NM_TIMEZONE "UTC0"
#endif
#ifndef NM_NTP_SERVER_1
#define NM_NTP_SERVER_1 "pool.ntp.org"
#endif
#ifndef NM_NTP_SERVER_2
#define NM_NTP_SERVER_2 "time.nist.gov"
#endif
#ifndef NM_NTP_SERVER_3
#define NM_NTP_SERVER_3 "time.google.com"
#endif
#ifndef NM_ENABLE_OTA
#define NM_ENABLE_OTA 0
#endif

#if NM_ENABLE_OTA && !defined(ARDUINO)
#error "NM_ENABLE_OTA requires the Arduino framework; other NightMare modules support ESP-IDF"
#endif
#ifndef NM_ENABLE_HTTP
#define NM_ENABLE_HTTP 0
#endif
#ifndef NM_ENABLE_WEBSOCKET
#define NM_ENABLE_WEBSOCKET 0
#endif
#ifndef NM_ENABLE_LVGL
#define NM_ENABLE_LVGL 0
#endif
// Runtime checking of action payloads against their declared arguments. The
// schema is published either way: it describes the action, and checking is an
// opt-in extra on top of that.
#ifndef NM_ENABLE_ACTION_PAYLOAD_ASSERTION
#define NM_ENABLE_ACTION_PAYLOAD_ASSERTION 0
#endif
// Whether a bound Remote resource also fetches and checks its owner's manifest.
//
// 1: a Remote resource subscribes to the owner's <device>/manifest/msgpack as
//    well as to /state, decodes the compact manifest and reports
//    kind/type/access/argument disagreements. Diagnostics only -- it has never
//    gated traffic.
//
// 0: a Remote resource subscribes to /state and nothing else. No manifest
//    subscription, no manifest parsing, no comparison. Everything else behaves
//    identically, because a manifest describes and /state tells the truth.
//
// Worth turning off on a device that is tight for memory, though it costs much
// less than it did: verification reads the compact manifest, which is roughly a
// fifth of the JSON one to transfer and to parse. What remains is a
// subscription per remote owner and that parse, landing during the connect
// burst, when retained manifests are replayed and the TLS session is still
// holding its record buffers. Manifest handlers are independent of this and
// still work either way.
#ifndef NM_ENABLE_REMOTE_RESOURCE_VERIFICATION
#define NM_ENABLE_REMOTE_RESOURCE_VERIFICATION 1
#endif
// Which encoding `>manifest` selects when asked for neither: json or msgpack.
#ifndef NM_DEFAULT_MANIFEST_FORMAT
#define NM_DEFAULT_MANIFEST_FORMAT msgpack
#endif
// How often a pending cleanup of a previous identity is retried. Deliberately
// slow: it only matters after an adoption, and each attempt publishes.
#ifndef NM_IDENTITY_CLEANUP_RETRY_MS
#define NM_IDENTITY_CLEANUP_RETRY_MS 60000UL
#endif
// Deferred framework publications retry with exponential backoff so a
// permanent failure cannot become work attempted on every cooperative tick.
#ifndef NM_SYSTEM_REQUEST_RETRY_MS
#define NM_SYSTEM_REQUEST_RETRY_MS 1000UL
#endif
#ifndef NM_SYSTEM_REQUEST_MAX_RETRY_MS
#define NM_SYSTEM_REQUEST_MAX_RETRY_MS 300000UL
#endif
#if NM_SYSTEM_REQUEST_RETRY_MS < 1
#error "NM_SYSTEM_REQUEST_RETRY_MS must be at least 1"
#endif
#if NM_SYSTEM_REQUEST_MAX_RETRY_MS < NM_SYSTEM_REQUEST_RETRY_MS
#error "NM_SYSTEM_REQUEST_MAX_RETRY_MS must not be shorter than NM_SYSTEM_REQUEST_RETRY_MS"
#endif
#if NM_SYSTEM_REQUEST_MAX_RETRY_MS > 2147483647UL
#error "NM_SYSTEM_REQUEST_MAX_RETRY_MS must fit the wrap-safe millis interval"
#endif
// Refresh of <device>/telemetry/network. Slow on purpose: it is bookkeeping,
// while /status owns presence.
#ifndef NM_NETWORK_TELEMETRY_INTERVAL_MS
#define NM_NETWORK_TELEMETRY_INTERVAL_MS 300000UL
#endif
// 1: startNightMareESP() gives the Scheduler its own FreeRTOS task.
// 0: nothing runs jobs except tickNightMareESP(), called from loop().
#ifndef NM_SCHEDULER_OWN_TASK
#define NM_SCHEDULER_OWN_TASK 1
#endif

// Current implementation targets ESP32. Reserved for a future platform split.
#ifndef NM_PLATFORM_ESP32
#define NM_PLATFORM_ESP32 1
#endif

#ifndef NM_CONSOLE_BUILTINS
#define NM_CONSOLE_BUILTINS 1
#endif
#ifndef NM_CONSOLE_SERIAL
#define NM_CONSOLE_SERIAL 0
#endif
// RX buffer of the console UART driver the serial console installs on ESP-IDF
// when the application has not: input waiting between console polls. Must be
// above the UART's 128-byte hardware FIFO.
#ifndef NM_SERIAL_RX_BUFFER
#define NM_SERIAL_RX_BUFFER 512
#endif

// First-boot default of the saved Wi-Fi station enable state (Config
// nightmare:connection:wifi_enabled): whether startNightMareESP() starts the IP
// station before anything was saved. WiFiIP_enable()/WiFiIP_disable() save it.
// The radio starts regardless whenever NM_ENABLE_WIFI_RADIO is on, so an
// ESP-NOW device can leave this off and never join an AP.
#ifndef NM_WIFI_AUTO
#define NM_WIFI_AUTO NM_ENABLE_WIFI
#endif

// First-boot default of the saved ESP-NOW enable state (Config
// nightmare:connection:espnow_enabled); EspNow_enable()/EspNow_disable() save
// it. Has no effect unless NM_NETWORK_ESPNOW is on.
#ifndef NM_ESPNOW_AUTO
#define NM_ESPNOW_AUTO 1
#endif
// First-boot default of the saved MQTT enable state.
#ifndef NM_MQTT_AUTO
#define NM_MQTT_AUTO NM_WIFI_AUTO
#endif
// Filters kept for Subscribe(), and per connection for SubscribeOn().
#ifndef NM_MAX_APP_SUBSCRIPTIONS
#define NM_MAX_APP_SUBSCRIPTIONS 64
#endif
// 0: OnMessage/OnConnect/OnNetworkStatus run on the network tasks.
// N > 0: their events are copied into an N-deep queue and run by
// NightMare::DeliverEvents(), from tickNightMareESP() unless NM_DELIVER_IN_TICK
// is 0 (then the application calls DeliverEvents() from the task it wants).
#ifndef NM_MESSAGE_QUEUE_DEPTH
#define NM_MESSAGE_QUEUE_DEPTH 0
#endif
#ifndef NM_DELIVER_IN_TICK
#define NM_DELIVER_IN_TICK 1
#endif

// NightMare Gateway (Gateway/NightMareGateway.h): an ESP-NOW broker for
// NightMare devices bridged to an MQTT broker, which is itself a NightMare
// device. NightMareGateway_init() / NightMareGateway_tick() take the place of
// startNightMareESP() / tickNightMareESP().
#ifndef NM_ENABLE_GATEWAY
#define NM_ENABLE_GATEWAY 0
#endif
// The local side: the ESP-NOW broker.
#ifndef NM_GATEWAY_ESPNOW
#define NM_GATEWAY_ESPNOW NM_ENABLE_GATEWAY
#endif
// The uplink: an MQTT client bridging the local side to a broker.
#ifndef NM_GATEWAY_MQTT
#define NM_GATEWAY_MQTT NM_ENABLE_GATEWAY
#endif
// 1: NightMareGateway_init() gives the gateway its own FreeRTOS task, woken as
//    soon as an ESP-NOW frame or an MQTT message arrives.
// 0: nothing runs the gateway except NightMareGateway_tick(), called from
//    loop(). The default for NightMareGatewayConfig::tickMode, which is chosen
//    at init, like the Scheduler's run mode.
#ifndef NM_GATEWAY_OWN_TASK
#define NM_GATEWAY_OWN_TASK 1
#endif
#ifndef NM_GATEWAY_TASK_STACK
#define NM_GATEWAY_TASK_STACK 12288
#endif
#ifndef NM_GATEWAY_TASK_PRIORITY
#define NM_GATEWAY_TASK_PRIORITY 5
#endif
// ESP-NOW frames the broker can hold between the radio callback and the gateway
// task (one slot is ~260 bytes). When it is full the frame is lost and the
// sender is not told, so it has to absorb the burst of devices (re)connecting at
// once: each one sends a retained hardware manifest of 20+ fragments. The storage
// is taken from PSRAM when the board has any, which makes a few hundred slots
// cheap; without PSRAM the broker falls back to NM_GATEWAY_ESPNOW_RX_QUEUE_INTERNAL
// slots in internal RAM.
#ifndef NM_GATEWAY_ESPNOW_RX_QUEUE
#define NM_GATEWAY_ESPNOW_RX_QUEUE 32
#endif
#ifndef NM_GATEWAY_ESPNOW_RX_QUEUE_INTERNAL
#define NM_GATEWAY_ESPNOW_RX_QUEUE_INTERNAL 32
#endif
// Messages waiting for the MQTT client (pointers only; the payloads live on the
// heap). Nothing leaves it until the session is up, so it also has to hold what
// the ESP-NOW side forwards while the broker connects.
#ifndef NM_GATEWAY_MQTT_OUTBOUND_QUEUE
#define NM_GATEWAY_MQTT_OUTBOUND_QUEUE 32
#endif
// Longest the own task sleeps with nothing arriving: the beacon, session
// pruning and link status are timed off this.
#ifndef NM_GATEWAY_TICK_MS
#define NM_GATEWAY_TICK_MS 50
#endif

#if NM_ENABLE_MQTT && !NM_ENABLE_NETWORK
#error "NM_ENABLE_MQTT requires NM_ENABLE_NETWORK"
#endif
#if NM_ENABLE_MQTT && !NM_ENABLE_WIFI
#error "NM_ENABLE_MQTT requires NM_ENABLE_WIFI: MQTT depends on the WiFiIP service"
#endif
#if NM_NETWORK_ESPNOW && !NM_ENABLE_NETWORK
#error "NM_NETWORK_ESPNOW requires NM_ENABLE_NETWORK"
#endif
#if NM_MESSAGE_QUEUE_DEPTH < 0
#error "NM_MESSAGE_QUEUE_DEPTH must be 0 (direct callbacks) or a queue depth"
#endif
#if NM_MAX_APP_SUBSCRIPTIONS < 1
#error "NM_MAX_APP_SUBSCRIPTIONS must be at least 1"
#endif
#if NM_ENABLE_NETWORK && !NM_ENABLE_RESOURCES
#error "NM_ENABLE_NETWORK requires NM_ENABLE_RESOURCES"
#endif
#if NM_ENABLE_TELEMETRY && !NM_ENABLE_SCHEDULER
#error "NM_ENABLE_TELEMETRY requires NM_ENABLE_SCHEDULER"
#endif
#if NM_ENABLE_TELEMETRY && !NM_ENABLE_NETWORK
#error "NM_ENABLE_TELEMETRY requires NM_ENABLE_NETWORK for automatic publication"
#endif
#if NM_ENABLE_JOBS && !NM_ENABLE_SCHEDULER
#error "NM_ENABLE_JOBS requires NM_ENABLE_SCHEDULER"
#endif
#if NM_ENABLE_SCHEDULER && !NM_ENABLE_SETTINGS
#error "NM_ENABLE_SCHEDULER requires NM_ENABLE_SETTINGS for persisted wall jobs"
#endif
#if NM_ENABLE_SCHEDULER && !NM_ENABLE_CONSOLE
#error "NM_ENABLE_SCHEDULER requires NM_ENABLE_CONSOLE for command dispatch"
#endif
#if NM_ENABLE_CONSOLE && !NM_ENABLE_SETTINGS
#error "NM_ENABLE_CONSOLE requires NM_ENABLE_SETTINGS in the current implementation"
#endif
#if NM_ENABLE_TIME_SYNC && !NM_ENABLE_WIFI
#error "NM_ENABLE_TIME_SYNC requires NM_ENABLE_WIFI"
#endif
#if NM_ENABLE_TIME_SYNC && !NM_ENABLE_SETTINGS
#error "NM_ENABLE_TIME_SYNC requires NM_ENABLE_SETTINGS"
#endif
#if NM_ENABLE_WIFI && !NM_ENABLE_SETTINGS
#error "NM_ENABLE_WIFI requires NM_ENABLE_SETTINGS in the current implementation"
#endif
#if NM_ENABLE_OTA && !NM_ENABLE_WIFI
#error "NM_ENABLE_OTA requires NM_ENABLE_WIFI"
#endif
#if NM_ENABLE_OTA && !NM_ENABLE_SETTINGS
#error "NM_ENABLE_OTA requires NM_ENABLE_SETTINGS"
#endif
#if NM_ENABLE_HTTP && !NM_ENABLE_NETWORK
#error "NM_ENABLE_HTTP requires NM_ENABLE_NETWORK"
#endif
#if NM_ENABLE_HTTP && !NM_ENABLE_CONSOLE
#error "NM_ENABLE_HTTP requires NM_ENABLE_CONSOLE for the /nm command endpoint"
#endif
#if NM_ENABLE_WEBSOCKET && !NM_ENABLE_HTTP
#error "NM_ENABLE_WEBSOCKET requires NM_ENABLE_HTTP"
#endif
#if NM_CONSOLE_SERIAL && !NM_ENABLE_CONSOLE
#error "NM_CONSOLE_SERIAL requires NM_ENABLE_CONSOLE"
#endif
#if NM_SERIAL_RX_BUFFER <= 128
#error "NM_SERIAL_RX_BUFFER must be above the UART's 128-byte hardware FIFO"
#endif
#if NM_WIFI_AUTO && !NM_ENABLE_WIFI
#error "NM_WIFI_AUTO requires NM_ENABLE_WIFI"
#endif
#if NM_ENABLE_WIFI && !NM_ENABLE_WIFI_RADIO
#error "NM_ENABLE_WIFI requires NM_ENABLE_WIFI_RADIO: the IP station runs on the radio"
#endif
#if NM_NETWORK_ESPNOW && !NM_ENABLE_WIFI_RADIO
#error "NM_NETWORK_ESPNOW requires NM_ENABLE_WIFI_RADIO: ESP-NOW runs on the Wi-Fi radio"
#endif
#if NM_ENABLE_GATEWAY && !NM_GATEWAY_ESPNOW && !NM_GATEWAY_MQTT
#error "NM_ENABLE_GATEWAY needs NM_GATEWAY_ESPNOW or NM_GATEWAY_MQTT: a gateway with neither side does nothing"
#endif
#if (NM_GATEWAY_ESPNOW || NM_GATEWAY_MQTT) && !NM_ENABLE_GATEWAY
#error "NM_GATEWAY_ESPNOW and NM_GATEWAY_MQTT require NM_ENABLE_GATEWAY"
#endif
// The gateway is a NightMare device: its identity, Configs, Resources and
// console come from NightMare (Gateway/GatewayNode).
#if NM_ENABLE_GATEWAY && !NM_ENABLE_RESOURCES
#error "NM_ENABLE_GATEWAY requires NM_ENABLE_RESOURCES: the gateway publishes its own Resources"
#endif
#if NM_ENABLE_GATEWAY && !NM_ENABLE_CONSOLE
#error "NM_ENABLE_GATEWAY requires NM_ENABLE_CONSOLE: the gateway answers <device>/console/in"
#endif
// The gateway carries its own traffic. NmConnection would inject a second
// publisher into the ResourcesManager and select a transport of its own.
#if NM_ENABLE_GATEWAY && !NM_ENABLE_NETWORK
#error "NM_ENABLE_GATEWAY requires NM_ENABLE_NETWORK: the gateway is a device whose transport is its own broker (ConnectionType::GATEWAY)"
#endif
#if NM_ENABLE_GATEWAY && NM_ENABLE_MQTT
#error "NM_ENABLE_GATEWAY requires NM_ENABLE_MQTT 0: the gateway's MQTT uplink is its own client (NM_GATEWAY_MQTT)"
#endif
#if NM_GATEWAY_ESPNOW && NM_NETWORK_ESPNOW
#error "NM_GATEWAY_ESPNOW and NM_NETWORK_ESPNOW both own ESP-NOW: a gateway is not also a client"
#endif
// Jobs would run on the Scheduler task, while the gateway's Resources and
// device table belong to whoever ticks the gateway.
#if NM_ENABLE_GATEWAY && NM_ENABLE_SCHEDULER && NM_SCHEDULER_OWN_TASK
#error "NM_ENABLE_GATEWAY requires NM_SCHEDULER_OWN_TASK 0: jobs run in the gateway tick (MANUAL), not in a task of their own"
#endif
#if NM_GATEWAY_TICK_MS < 1
#error "NM_GATEWAY_TICK_MS must be at least 1"
#endif
