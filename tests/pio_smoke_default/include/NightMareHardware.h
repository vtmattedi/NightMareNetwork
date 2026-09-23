#pragma once
#include <NightMare/HardwareProfile.h>

namespace NMHardware
{
inline Profile projectProfile()
{
    static const Device devices[] = {
        {"button", "momentary-switch"},
    };
    static const Connection pins[] = {
        {0, 0, "pressed", NoBus, SignalType::Gpio, Direction::Input, Pull::Up, true},
    };
    return {"esp32-devkit:test", devices, 1, pins, 1};
}
}
