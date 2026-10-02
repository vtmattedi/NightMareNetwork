#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_WIFI

#include "NmWifiEsp.h"

// NightMare-side IP station integration: stored profile, device hostname, the
// IP-link report to NmConnection and the services that follow the first
// connection (OTA, SNTP). The ESP-IDF driver in NmWifiEsp knows none of this.
// Runs after the framework's first-connection services, on the station's
// monitor task (not the ESP event task).
using WiFiConnectedCallback = void (*)(bool firstConnection);
void WiFi_onConnected(WiFiConnectedCallback callback);

namespace NightMare
{
WiFiProfile WiFiStoredProfile();
// Prepares the station (radio, state reporting). With the network layer built, NmConnection
// owns when the station runs -- only while an IP-based connection is selected -- so this does
// not join the AP itself. Without it, it starts the station straight away.
bool WiFiBegin();
// Joins the AP with the stored profile. Idempotent while connecting/connected.
bool WiFiStationResume();
// Leaves the AP and stops the station; the stored profile is kept and the radio stays up.
// Frees the channel, so ESP-NOW may hop.
void WiFiStationSuspend();
// Applies the profile to the running stack and persists it once it works.
// While the stack is stopped it is only persisted.
bool WiFiApplyProfile(const WiFiProfile &profile);
}

#endif // NM_ENABLE_WIFI
