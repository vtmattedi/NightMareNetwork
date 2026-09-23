#pragma once
#include <NightMare/HardwareProfile.h>

namespace NMHardware
{
inline Profile projectProfile()
{
    static const Board boards[] = {
        {"main", "esp32-devkit:test"},
        {"io", "button-board:test"},
    };
    static const Device devices[] = {
        {"button", "momentary-switch", 1},
    };
    static const Connection pins[] = {
        {0, 0, "pressed", NoBus, SignalType::Gpio, Direction::Input, Pull::Up, true},
    };
    return {boards, 2, devices, 1, pins, 1};
}
}
