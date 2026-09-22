#pragma once
#include <NightMare/HardwareProfile.h>

namespace NMHardware
{
inline Profile projectProfile()
{
    static const Connection pins[] = {
        {"button", 0, Direction::Input, Pull::Up, true, "test pin"},
    };
    return {"ESP32 DevKit test board", pins, 1};
}
}
