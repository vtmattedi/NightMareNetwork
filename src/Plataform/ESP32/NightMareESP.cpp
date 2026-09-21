#include "NightMareESP.h"

#include <Core/DeviceIdentity.h>
#if NM_ENABLE_SCHEDULER
#include <Core/Scheduler.h>
#endif
#if NM_ENABLE_TELEMETRY
#include <Core/Telemetry.h>
#endif
#if NM_ENABLE_WIFI
#include "NightMareWIFI.h"
#endif

void startNightMareESP()
{
    gDeviceIdentity.begin();
#if NM_ENABLE_SCHEDULER
    gScheduler.begin();
#endif
#if NM_ENABLE_TELEMETRY
    Telemetry.start();
#endif
#if NM_ENABLE_WIFI
    Serial.println("Initializing WiFi...");
    WiFi_Auto();
#endif
}
