#pragma once
#include <core/Logs.h>
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

#if NM_ENABLE_MQTT && !NM_ENABLE_NETWORK
#error "NM_ENABLE_MQTT requires NM_ENABLE_NETWORK"
#endif
#if NM_ENABLE_NETWORK && !NM_ENABLE_RESOURCES
#error "NM_ENABLE_NETWORK requires NM_ENABLE_RESOURCES"
#endif
#if NM_ENABLE_TELEMETRY && !NM_ENABLE_RESOURCES
#error "NM_ENABLE_TELEMETRY requires NM_ENABLE_RESOURCES"
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
