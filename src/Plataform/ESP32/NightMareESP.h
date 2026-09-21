#pragma once

#include <NightMare/Features.h>

// Optional one-call startup after the project registers its resource handlers.
// Identity, scheduler, telemetry and WiFi/MQTT are initialized as enabled.
void startNightMareESP();
