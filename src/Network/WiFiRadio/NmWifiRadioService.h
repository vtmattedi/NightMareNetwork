#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_WIFI_RADIO

#include "NmWifiRadio.h"

// NightMare-side radio integration: reports radio availability to NmConnection
// (ESP-NOW starts on it), and stops the IP station when the radio goes away.
// The ESP-IDF driver in NmWifiRadio knows none of this.
namespace NightMare
{
// Starts the radio and reports it. Idempotent. startNightMareESP() calls it,
// and WiFiBegin() does too, so the station never runs on an unreported radio.
bool WiFiRadioBegin();
// Stops the IP station (if running) and the radio.
void WiFiRadioEnd();
}

#endif // NM_ENABLE_WIFI_RADIO
