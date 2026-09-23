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
        {"temperature", "DS18B20", NoBoard},
    };
    static const Connection pins[] = {
        {0, 0, "pressed", NoBus, SignalType::Gpio, Direction::Input, Pull::Up, true},
        {4, 1, "data", 0, SignalType::OneWire, Direction::Bidirectional,
         Pull::ExternalUp, false, Resistor("4k7")},
    };
    return {boards, 2, devices, 2, pins, 2};
}
}
