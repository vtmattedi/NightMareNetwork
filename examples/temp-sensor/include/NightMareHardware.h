#pragma once

#include <NightMare/HardwareProfile.h>
#include <board.h>

namespace NMHardware
{
inline Profile projectProfile()
{
    static const Board boards[] = {
        {"main", BOARD_NAME},
    };
    static const Device devices[] = {
        {"temperature", "DS18B20", 0},
    };
    static const Connection connections[] = {
        {PIN_ONE_WIRE, 0, "data", 0, SignalType::OneWire,
         Direction::Bidirectional, Pull::ExternalUp, false, Resistor("4k7")},
    };

    return {
        boards, sizeof(boards) / sizeof(boards[0]),
        devices, sizeof(devices) / sizeof(devices[0]),
        connections, sizeof(connections) / sizeof(connections[0])
    };
}
}
