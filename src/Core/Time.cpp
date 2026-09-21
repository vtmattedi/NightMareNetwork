#include "Time.h"

#include <sys/time.h>

namespace NightMare
{
namespace Time
{
namespace
{
constexpr time_t EarliestValidEpoch = 1577836800; // 2020-01-01 UTC

tm utcParts(time_t epoch)
{
    tm result{};
    gmtime_r(&epoch, &result);
    return result;
}
}

time_t now()
{
    return ::time(nullptr);
}

bool valid()
{
    return now() >= EarliestValidEpoch;
}

bool setEpoch(time_t epoch)
{
    if (epoch < EarliestValidEpoch)
        return false;
    const timeval value{epoch, 0};
    return settimeofday(&value, nullptr) == 0;
}

int second(time_t epoch) { return utcParts(epoch).tm_sec; }
int minute(time_t epoch) { return utcParts(epoch).tm_min; }
int hour(time_t epoch) { return utcParts(epoch).tm_hour; }
int day(time_t epoch) { return utcParts(epoch).tm_mday; }
int month(time_t epoch) { return utcParts(epoch).tm_mon + 1; }
int year(time_t epoch) { return utcParts(epoch).tm_year + 1900; }
}
}
