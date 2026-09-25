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
#ifndef NM_ENABLE_RESOURCES
#define NM_ENABLE_RESOURCES 1
#endif
#ifndef NM_ENABLE_NETWORK
#define NM_ENABLE_NETWORK 1
#endif
#ifndef NM_ENABLE_CONSOLE
#define NM_ENABLE_CONSOLE 1
#endif
#ifndef NM_ENABLE_WIFI
#define NM_ENABLE_WIFI 1
#endif
#ifndef NM_ENABLE_MQTT
#define NM_ENABLE_MQTT 1
#endif
#ifndef NM_ENABLE_TELEMETRY
#define NM_ENABLE_TELEMETRY 1
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
// Which encoding `>manifest` returns when asked for neither: json or mpack.
#ifndef NM_DEFAULT_MANIFEST_FORMAT
#define NM_DEFAULT_MANIFEST_FORMAT json
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

// Whether WiFi_Auto() is called from startNightMareESP(). If not, the application must call it itself.
// If NM_ENABLE_WIFI is 0, this has no effect.
#ifndef NM_WIFI_AUTO
#define NM_WIFI_AUTO NM_ENABLE_WIFI
#endif

#if NM_ENABLE_MQTT && !NM_ENABLE_NETWORK
#error "NM_ENABLE_MQTT requires NM_ENABLE_NETWORK"
#endif
#if NM_ENABLE_NETWORK && !NM_ENABLE_RESOURCES
#error "NM_ENABLE_NETWORK requires NM_ENABLE_RESOURCES"
#endif
#if NM_ENABLE_TELEMETRY && !NM_ENABLE_SCHEDULER
#error "NM_ENABLE_TELEMETRY requires NM_ENABLE_SCHEDULER"
#endif
#if NM_ENABLE_TELEMETRY && !NM_ENABLE_MQTT
#error "NM_ENABLE_TELEMETRY requires NM_ENABLE_MQTT for automatic publication"
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
#if NM_WIFI_AUTO && !NM_ENABLE_WIFI
#error "NM_WIFI_AUTO requires NM_ENABLE_WIFI"
#endif
