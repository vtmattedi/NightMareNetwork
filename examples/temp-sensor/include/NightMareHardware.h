#pragma once

#include <NightMare/HardwareProfile.h>
#include <board.h>

namespace NMHardware
{
inline Profile projectProfile()
{
    static const Board boards[] = {
        {"controller", BOARD_NAME},
        {"probe", "ds18b20-probe:v1"},
    };
    static const Device devices[] = {
        {"temperature", "DS18B20", 1, DeviceKind::Sensor, "waterproof-probe"},
    };
    static const Net nets[] = {
        {"temperature_data", SignalType::OneWire, 0, Direction::Bidirectional,
         Pull::ExternalUp, false, Resistor("4k7")},
    };
    static const Connection connections[] = {
        {{EndpointKind::Board, 0, "ONE_WIRE_GPIO"},
         {EndpointKind::Board, 1, "DATA"}, 0},
        {{EndpointKind::Board, 1, "DATA"},
         {EndpointKind::Device, 0, "DQ"}, 0},
    };

    return {
        0,
        boards, sizeof(boards) / sizeof(boards[0]),
        devices, sizeof(devices) / sizeof(devices[0]),
        nets, sizeof(nets) / sizeof(nets[0]),
        connections, sizeof(connections) / sizeof(connections[0])
    };
}
}
