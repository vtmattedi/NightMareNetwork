#include <NightMare/Features.h>
#if NM_ENABLE_TIME_SYNC

#include "TimeSyncronization.h"

#include <Core/Logs.h>
#include <Core/StateStore.h>
#include <Core/Time.h>
#include <WiFi.h>
#include <atomic>
#include <esp_sntp.h>

namespace
{
std::atomic<bool> syncPending{false};
void (*timeSyncCallback)(void) = nullptr;

void sntpTimeAvailable(timeval *)
{
    // This runs on lwIP's task. RuntimeState owns Arduino Strings and is not
    // thread-safe, so defer all bookkeeping and user callbacks to loop().
    syncPending.store(true, std::memory_order_release);
}

bool recordSynchronizedClock()
{
    const time_t timestamp = NightMare::Time::now();
    if (!NightMare::Time::valid())
        return false;

    SystemState.setFlag("time_synced", true);
    SystemState.set("boot_time", String(static_cast<unsigned long>(timestamp - millis() / 1000)));
    if (timeSyncCallback != nullptr)
        timeSyncCallback();
    return true;
}
}

bool startSntpTimeSync()
{
    if (WiFi.status() != WL_CONNECTED)
        return false;

    const char *timezone = getenv("TZ");
    if (timezone == nullptr || timezone[0] == '\0')
        timezone = NM_TIMEZONE;

    SystemState.setFlag("time_synced", false);
    esp_sntp_set_time_sync_notification_cb(sntpTimeAvailable);
    configTzTime(timezone, NM_NTP_SERVER_1, NM_NTP_SERVER_2, NM_NTP_SERVER_3);
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
