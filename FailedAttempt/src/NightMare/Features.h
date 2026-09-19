#pragma once

// Set these to 0 in build_flags for smaller applications. Low-level headers
// remain directly usable; flags govern the standard facade and umbrella API.
#ifndef NIGHTMARE_ENABLE_SETTINGS
#define NIGHTMARE_ENABLE_SETTINGS 1
#endif
#ifndef NIGHTMARE_ENABLE_CONSOLE
#define NIGHTMARE_ENABLE_CONSOLE 1
#endif
#ifndef NIGHTMARE_ENABLE_TELEMETRY
#define NIGHTMARE_ENABLE_TELEMETRY 1
#endif
#ifndef NIGHTMARE_ENABLE_OTA
#define NIGHTMARE_ENABLE_OTA 1
#endif
#ifndef NIGHTMARE_ENABLE_WIFI
#define NIGHTMARE_ENABLE_WIFI 1
#endif
#ifndef NIGHTMARE_ENABLE_MQTT
#define NIGHTMARE_ENABLE_MQTT 1
#endif
