#include <NightMare/Core/Time.h>
#include <sys/time.h>

namespace NightMare { namespace Time {
time_t now() { return ::time(nullptr); }
bool valid() { return now() >= 1577836800; } // 2020-01-01 UTC
bool setEpoch(time_t epoch) {
    if (epoch < 1577836800) return false;
    timeval value = {epoch, 0};
    return settimeofday(&value, nullptr) == 0;
}
static tm parts(time_t epoch) { tm out{}; gmtime_r(&epoch, &out); return out; }
int second(time_t epoch) { return parts(epoch).tm_sec; }
int minute(time_t epoch) { return parts(epoch).tm_min; }
int hour(time_t epoch) { return parts(epoch).tm_hour; }
int day(time_t epoch) { return parts(epoch).tm_mday; }
int month(time_t epoch) { return parts(epoch).tm_mon + 1; }
int year(time_t epoch) { return parts(epoch).tm_year + 1900; }
} } // namespace NightMare::Time
