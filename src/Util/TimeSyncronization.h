#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_TIME_SYNC
#include <Arduino.h>

/// Starts the ESP32 SNTP client. Completion is dispatched by
/// processTimeSyncEvents() from tickNightMareESP().
bool startSntpTimeSync();

/// Compatibility name for the former blocking HTTP implementation.
[[deprecated("HTTP time sync was removed; use startSntpTimeSync()")]]
bool autoSyncTime();

void manualSyncTime(unsigned long timestamp);
void onTimeSync(void (*callback)(void));

/// Applies a completed SNTP update to NightMare runtime state. Applications
/// using startNightMareESP()/tickNightMareESP() do not call this directly.
void processTimeSyncEvents();
#endif // NM_ENABLE_TIME_SYNC
