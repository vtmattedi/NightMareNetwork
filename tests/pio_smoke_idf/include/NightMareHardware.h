#pragma once
#include <NightMare/HardwareProfile.h>

namespace NMHardware
{
inline Profile projectProfile()
{
    static const Terminal mcuTerminals[] = {
        {"GPIO4"}, {"3V3", CanonicalNet::V3v3}, {"GND", CanonicalNet::Gnd}};
    static const Device controllerDevices[] = {
        {"mcu", mcuTerminals, 3, "ESP32-C3", "mcu", "ESP32-C3"}};
    static const ConnectorContact sensorContacts[] = {
        {"VDD", CanonicalNet::V3v3}, {"DATA"}, {"GND", CanonicalNet::Gnd}};
    static const Connector controllerConnectors[] = {
        {"j_temp", sensorContacts, 3, "Temperature probe", ConnectorKind::Jst}};
    static const Connection controllerConnections[] = {
        {{"", EndpointKind::DeviceTerminal, "mcu", "GPIO4"},
         {"", EndpointKind::ConnectorContact, "j_temp", "DATA"}},
        {{"", EndpointKind::DeviceTerminal, "mcu", "3V3"},
         {"", EndpointKind::ConnectorContact, "j_temp", "VDD"}},
        {{"", EndpointKind::DeviceTerminal, "mcu", "GND"},
         {"", EndpointKind::ConnectorContact, "j_temp", "GND"}},
    };
    static const Terminal sensorTerminals[] = {
        {"VDD", CanonicalNet::V3v3}, {"DATA"}, {"GND", CanonicalNet::Gnd}};
    static const Device probeDevices[] = {
        {"sensor", sensorTerminals, 3, "Temperature sensor", "sensor", "DS18B20"}};
    static const Connector probeConnectors[] = {
        {"lead", sensorContacts, 3, "Probe lead", ConnectorKind::DirectWire}};
    static const Connection probeConnections[] = {
        {{"", EndpointKind::DeviceTerminal, "sensor", "VDD"},
         {"", EndpointKind::ConnectorContact, "lead", "VDD"}},
        {{"", EndpointKind::DeviceTerminal, "sensor", "DATA"},
         {"", EndpointKind::ConnectorContact, "lead", "DATA"}},
        {{"", EndpointKind::DeviceTerminal, "sensor", "GND"},
         {"", EndpointKind::ConnectorContact, "lead", "GND"}},
    };
    static const HardwareDefinition definitions[] = {
        {"esp32-controller-test", AssemblyKind::CustomBoard, "Test controller",
         "esp32-devkit:test", nullptr,
         {nullptr, 0, controllerDevices, 1, controllerConnectors, 1,
          controllerConnections, 3}},
        {"ds18b20-probe-test", AssemblyKind::SensorProbe, "Waterproof probe",
         "DS18B20 probe", nullptr,
         {nullptr, 0, probeDevices, 1, probeConnectors, 1, probeConnections, 3}},
    };
    static const Assembly roots[] = {
        {"controller", "esp32-controller-test", "Controller"},
        {"probe", "ds18b20-probe-test", "Water temperature probe"},
    };
    static const Connection connections[] = {
        {{"controller", EndpointKind::ConnectorContact, "j_temp", "VDD"},
         {"probe", EndpointKind::ConnectorContact, "lead", "VDD"}, {"red"}},
        {{"controller", EndpointKind::ConnectorContact, "j_temp", "DATA"},
         {"probe", EndpointKind::ConnectorContact, "lead", "DATA"}, {"yellow"}},
        {{"controller", EndpointKind::ConnectorContact, "j_temp", "GND"},
         {"probe", EndpointKind::ConnectorContact, "lead", "GND"}, {"black"}},
    };
    return {"controller", definitions, 2, roots, 2, connections, 3};
}
}
