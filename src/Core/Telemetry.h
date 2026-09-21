#pragma once
#include <NightMare/Features.h>

#if NM_ENABLE_TELEMETRY
#include <Arduino.h>

// Device-wide information. Sensors and actuators belong to NetResources.
class TelemetryService
{
public:
    // Installs a monotonic job that runs the built-in TELEMETRY PUBLISH command.
    bool start(uint32_t intervalMs = NM_TELEMETRY_INTERVAL_MS);
    bool publish();
    String snapshotJson() const;

private:
    bool started_ = false;
    uint32_t intervalMs_ = 0;
};

extern TelemetryService Telemetry;
#endif // NM_ENABLE_TELEMETRY
