#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_WIFI_RADIO

#include "NmWifiRadio.h"
#include <Network/Connectivity.h>

// NightMare-side radio integration: reports dependency state to WiFiIP,
// ESP-NOW, telemetry, and routing.
// The ESP-IDF driver in NmWifiRadio knows none of this.
namespace NightMare
{
// Starts the radio and reports it. Idempotent. startNightMareESP() calls it,
// and WiFiBegin() does too, so the station never runs on an unreported radio.
bool WiFiRadioBegin();
// Stops the IP station (if running) and the radio.
void WiFiRadioEnd();
bool WiFiRadio_supported();
bool WiFiRadio_enabled();
ConnectivityState WiFiRadio_state();
}

#endif // NM_ENABLE_WIFI_RADIO
