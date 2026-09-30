#include <NightMare/Features.h>
#if NM_ENABLE_WIFI_RADIO

#include "NmWifiRadioService.h"

#if NM_ENABLE_NETWORK
#include <Network/NmConnectionInternal.h>
#endif
#if NM_ENABLE_WIFI
#include <Network/WiFiIP/NmWifiEsp.h>
#endif

namespace
{
bool reportedRunning = false;

void onRadioState(bool running)
{
    reportedRunning = running;
#if NM_ENABLE_WIFI
    // The station runs on the radio: drop it before the driver goes away,
    // rather than leave it retrying against a stopped radio.
    if (!running && WiFi_state() != NightMare::WiFiState::STOPPED)
        WiFi_stop();
#endif
#if NM_ENABLE_NETWORK
    // Availability only; NmConnection decides what to start or stop on it.
    NightMare::OnRadioAvailabilityIngress(running);
#endif
}
}

namespace NightMare
{
bool WiFiRadioBegin()
{
    WiFiRadio_onState(onRadioState);
    // Started earlier by something that went straight to the driver, before
    // this callback existed: report it now instead of never.
    if (WiFiRadio_running())
    {
        if (!reportedRunning)
            onRadioState(true);
        return true;
    }
    return WiFiRadio_start();
}

void WiFiRadioEnd()
{
    WiFiRadio_stop();
}
}

#endif // NM_ENABLE_WIFI_RADIO
