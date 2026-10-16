#include <NightMare/Features.h>
#if NM_ENABLE_TIME_SYNC

#include "TimeSyncronization.h"

#include <Core/Logs.h>
#include <Core/SystemState.h>
#include <Core/Time.h>
#include <Network/WiFiIP/NmWifiEsp.h>
#include <atomic>
#include <esp_sntp.h>
#include <cstdlib>
#include <ctime>

namespace
{
std::atomic<bool> syncPending{false};
void (*timeSyncCallback)(void) = nullptr;

void sntpTimeAvailable(timeval *)
{
    // This runs on lwIP's task. Keep state transitions and user callbacks in
    // the normal cooperative context.
    syncPending.store(true, std::memory_order_release);
}

bool recordSynchronizedClock()
{
    if (!NightMare::Time::valid())
        return false;

    SystemState.set(SystemFlag::TimeSynced);
    if (timeSyncCallback != nullptr)
        timeSyncCallback();
    return true;
}
}

bool startSntpTimeSync()
{
    if (WiFi_state() != NightMare::WiFiState::CONNECTED)
        return false;

    const char *timezone = getenv("TZ");
    if (timezone == nullptr || timezone[0] == '\0')
        timezone = NM_TIMEZONE;

    SystemState.clear(SystemFlag::TimeSynced);
    esp_sntp_set_time_sync_notification_cb(sntpTimeAvailable);
    if (setenv("TZ", timezone, 1) != 0)
        return false;
    tzset();
    if (esp_sntp_enabled())
        esp_sntp_stop();
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, NM_NTP_SERVER_1);
#if SNTP_MAX_SERVERS > 1
    esp_sntp_setservername(1, NM_NTP_SERVER_2);
#endif
#if SNTP_MAX_SERVERS > 2
    esp_sntp_setservername(2, NM_NTP_SERVER_3);
#endif
    esp_sntp_init();
    LOG("Time", "SNTP synchronization started (%s)", timezone);
    return true;
}

bool autoSyncTime()
{
    return startSntpTimeSync();
}

void manualSyncTime(unsigned long timestamp)
{
    if (NightMare::Time::setEpoch(timestamp) && recordSynchronizedClock())
        LOG("Time", "Clock synchronized manually");
}

void onTimeSync(void (*callback)(void))
{
    timeSyncCallback = callback;
}

void processTimeSyncEvents()
{
    if (!syncPending.exchange(false, std::memory_order_acq_rel))
        return;
    if (recordSynchronizedClock())
        LOG("Time", "Clock synchronized by SNTP");
}

#endif // NM_ENABLE_TIME_SYNC
