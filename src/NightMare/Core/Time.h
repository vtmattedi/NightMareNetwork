#pragma once
#include <stdint.h>
#include <time.h>

namespace NightMare { namespace Time {
time_t now();
bool valid();
bool setEpoch(time_t epoch);
int second(time_t epoch = now());
int minute(time_t epoch = now());
int hour(time_t epoch = now());
int day(time_t epoch = now());
int month(time_t epoch = now());
int year(time_t epoch = now());
} } // namespace NightMare::Time
