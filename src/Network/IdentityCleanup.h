#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_MQTT
#include <Arduino.h>

enum class IdentityCleanupResult : uint8_t
{
    NOTHING_PENDING, // No adoption left anything behind.
    COMPLETE,        // This attempt finished the cleanup.
    PENDING          // Something is left: offline, a failure, or a reboot still due.
};

/// @brief One attempt at removing what a previous identity left retained on the
/// broker: its resource footprint and its status. Each part is marked done only
/// once it succeeded, so this is safe to call as often as wanted: from the
/// startup retry job, a test, or by hand in a build without the Scheduler.
IdentityCleanupResult processPendingIdentityCleanup();
#endif // NM_ENABLE_MQTT
