#pragma once

#include <NightMare/HardwareProfile.h>
#include <board.h>

namespace NMHardware
{
inline Profile projectProfile()
{
    static const Terminal controllerTerminals[] = {
        {"ONE_WIRE_GPIO"}, {"3V3", CanonicalNet::V3v3}, {"GND", CanonicalNet::Gnd}};
    static const Device controllerDevices[] = {
        {"mcu", controllerTerminals, 3, "Controller MCU", "mcu"}};
    static const ConnectorContact contacts[] = {
        {"VDD", CanonicalNet::V3v3}, {"DATA"}, {"GND", CanonicalNet::Gnd}};
    static const Connector controllerConnectors[] = {
        {"j_temp", contacts, 3, "Temperature probe", ConnectorKind::Jst}};
    static const Connection controllerConnections[] = {
        {{"", EndpointKind::DeviceTerminal, "mcu", "ONE_WIRE_GPIO"},
         {"", EndpointKind::ConnectorContact, "j_temp", "DATA"}},
        {{"", EndpointKind::DeviceTerminal, "mcu", "3V3"},
         {"", EndpointKind::ConnectorContact, "j_temp", "VDD"}},
        {{"", EndpointKind::DeviceTerminal, "mcu", "GND"},
         {"", EndpointKind::ConnectorContact, "j_temp", "GND"}},
    };
    static const Terminal probeTerminals[] = {
        {"VDD", CanonicalNet::V3v3}, {"DATA"}, {"GND", CanonicalNet::Gnd}};
    static const Device probeDevices[] = {
        {"sensor", probeTerminals, 3, "Temperature sensor", "sensor", "DS18B20"}};
    static const Connector probeConnectors[] = {
        {"lead", contacts, 3, "Probe lead", ConnectorKind::DirectWire}};
    static const Connection probeConnections[] = {
        {{"", EndpointKind::DeviceTerminal, "sensor", "VDD"},
         {"", EndpointKind::ConnectorContact, "lead", "VDD"}},
        {{"", EndpointKind::DeviceTerminal, "sensor", "DATA"},
         {"", EndpointKind::ConnectorContact, "lead", "DATA"}},
        {{"", EndpointKind::DeviceTerminal, "sensor", "GND"},
         {"", EndpointKind::ConnectorContact, "lead", "GND"}},
    };
    static const HardwareDefinition definitions[] = {
        {"temperature-controller", AssemblyKind::CustomBoard, "Temperature controller",
         BOARD_NAME, nullptr,
         {nullptr, 0, controllerDevices, 1, controllerConnectors, 1,
          controllerConnections, 3}},
        {"ds18b20-probe", AssemblyKind::SensorProbe, "Waterproof probe",
         "DS18B20 probe", nullptr,
         {nullptr, 0, probeDevices, 1, probeConnectors, 1, probeConnections, 3}},
    };
    static const Assembly roots[] = {
        {"controller", "temperature-controller", "Controller"},
        {"probe", "ds18b20-probe", "Water temperature probe"},
    };
    static const Connection connections[] = {
        {{"controller", EndpointKind::ConnectorContact, "j_temp", "VDD"},
         {"probe", EndpointKind::ConnectorContact, "lead", "VDD"}},
        {{"controller", EndpointKind::ConnectorContact, "j_temp", "DATA"},
         {"probe", EndpointKind::ConnectorContact, "lead", "DATA"}},
        {{"controller", EndpointKind::ConnectorContact, "j_temp", "GND"},
         {"probe", EndpointKind::ConnectorContact, "lead", "GND"}},
    };
    return {"controller", definitions, 2, roots, 2, connections, 3};
}
}
