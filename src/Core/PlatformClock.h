#pragma once

#include <cstdint>
#include <esp_timer.h>

namespace NightMare
{
namespace Platform
{
inline uint32_t monotonicMillis()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}
}
}
