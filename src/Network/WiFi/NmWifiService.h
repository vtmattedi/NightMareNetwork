#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_WIFI

#include "NmWifiEsp.h"

// NightMare-side WiFi integration: stored profile, device hostname and the
// services that follow the first connection. The ESP-IDF driver in NmWifiEsp
// knows none of this.
// Runs after the framework's first-connection services, on the ESP event task.
using WiFiConnectedCallback = void (*)(bool firstConnection);
void WiFi_onConnected(WiFiConnectedCallback callback);

namespace NightMare
{
WiFiProfile WiFiStoredProfile();
bool WiFiBegin();
// Applies the profile to the running stack and persists it once it works.
// While the stack is stopped it is only persisted.
bool WiFiApplyProfile(const WiFiProfile &profile);
}

#endif // NM_ENABLE_WIFI
