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
        {"temperature", "DS18B20", 1, DeviceKind::Sensor, "waterproof-probe"},
        {"panel", "generic-component", 1, DeviceKind::Unknown, "panel-mount"},
    };
    static const Net nets[] = {
        {"button", SignalType::Gpio, NoBus, Direction::Input, Pull::Up, true},
        {"temperature_data", SignalType::OneWire, 0, Direction::Bidirectional,
         Pull::ExternalUp, false, Resistor("4k7")},
    };
    static const Connection connections[] = {
        {{EndpointKind::Board, 0, "GPIO0"}, {EndpointKind::Board, 1, "BUTTON"}, 0},
        {{EndpointKind::Board, 1, "BUTTON"}, {EndpointKind::Device, 0, "1"}, 0},
        {{EndpointKind::Board, 0, "GPIO4"}, {EndpointKind::Board, 1, "TEMP"}, 1},
        {{EndpointKind::Board, 1, "TEMP"}, {EndpointKind::Device, 1, "DQ"}, 1},
    };
    return {0, boards, 2, devices, 3, nets, 2, connections, 4};
}
}
