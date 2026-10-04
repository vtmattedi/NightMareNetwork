#include <NightMare/Features.h>
#if NM_ENABLE_WIFI_RADIO

#include "NmWifiRadioService.h"

#if NM_ENABLE_NETWORK
#include <Network/NmConnectionInternal.h>
#endif
#if NM_ENABLE_WIFI
#include <Network/WiFiIP/NmWifiService.h>
#endif
#if NM_NETWORK_ESPNOW
#include <Network/EspNow/NmEspNowConnection.h>
#endif

namespace
{
bool reportedRunning = false;
bool radioEnabled = false;
bool radioError = false;

void onRadioState(bool running)
{
    reportedRunning = running;
#if NM_ENABLE_WIFI
    NightMare::WiFiIP_onRadioState(running);
#endif
#if NM_NETWORK_ESPNOW
    NightMare::EspNow_onRadioState(running);
#endif
#if NM_ENABLE_NETWORK
    // Availability only; dependent services and routing react to the fact.
    NightMare::OnRadioAvailabilityIngress(running);
#endif
}
}

namespace NightMare
{
bool WiFiRadioBegin()
{
    radioEnabled = true;
    WiFiRadio_onState(onRadioState);
    // Started earlier by something that went straight to the driver, before
    // this callback existed: report it now instead of never.
    if (WiFiRadio_running())
    {
        radioError = false;
        if (!reportedRunning)
            onRadioState(true);
        return true;
    }
    const bool started = WiFiRadio_start();
    radioError = !started;
    return started;
}

bool WiFiRadio_supported() { return true; }
bool WiFiRadio_enabled() { return radioEnabled; }
ConnectivityState WiFiRadio_state()
{
    if (WiFiRadio_running())
        return ConnectivityState::READY;
    return radioError ? ConnectivityState::ERROR : ConnectivityState::STOPPED;
}

void WiFiRadioEnd()
{
    radioEnabled = false;
    radioError = false;
    WiFiRadio_stop();
}
}

#endif // NM_ENABLE_WIFI_RADIO
