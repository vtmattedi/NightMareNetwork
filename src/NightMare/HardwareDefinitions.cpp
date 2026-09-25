#include "HardwareDefinitions.h"

namespace NMHardware
{
namespace
{
constexpr size_t countOf(size_t bytes, size_t item) { return bytes / item; }

const HardwareDefinition &espDefinition()
{
    static const Terminal mcuTerminals[] = {
        {"GPIO0"}, {"GPIO1"}, {"GPIO2"}, {"GPIO3"}, {"GPIO4"}, {"GPIO5"},
        {"GPIO6"}, {"GPIO7"}, {"GPIO8"}, {"GPIO9"}, {"GPIO10"},
        {"3V3", CanonicalNet::V3v3}, {"5V", CanonicalNet::V5v},
        {"GND", CanonicalNet::Gnd}};
    static const Device devices[] = {
        {"mcu", mcuTerminals, countOf(sizeof(mcuTerminals), sizeof(mcuTerminals[0])),
         "ESP32-C3", "mcu", "ESP32-C3"}};
    static const ConnectorContact headerContacts[] = {
        {"GPIO0"}, {"GPIO1"}, {"GPIO2"}, {"GPIO3"}, {"GPIO4"}, {"GPIO5"},
        {"GPIO6"}, {"GPIO7"}, {"GPIO8"}, {"GPIO9"}, {"GPIO10"},
        {"3V3", CanonicalNet::V3v3}, {"5V", CanonicalNet::V5v},
        {"GND", CanonicalNet::Gnd}};
    static const Connector connectors[] = {
        {"headers", headerContacts,
         countOf(sizeof(headerContacts), sizeof(headerContacts[0])),
         "Castellated headers", ConnectorKind::Header}};
    static const Connection connections[] = {
        {{"", EndpointKind::DeviceTerminal, "mcu", "GPIO0"}, {"", EndpointKind::ConnectorContact, "headers", "GPIO0"}},
        {{"", EndpointKind::DeviceTerminal, "mcu", "GPIO1"}, {"", EndpointKind::ConnectorContact, "headers", "GPIO1"}},
        {{"", EndpointKind::DeviceTerminal, "mcu", "GPIO2"}, {"", EndpointKind::ConnectorContact, "headers", "GPIO2"}},
        {{"", EndpointKind::DeviceTerminal, "mcu", "GPIO3"}, {"", EndpointKind::ConnectorContact, "headers", "GPIO3"}},
        {{"", EndpointKind::DeviceTerminal, "mcu", "GPIO4"}, {"", EndpointKind::ConnectorContact, "headers", "GPIO4"}},
        {{"", EndpointKind::DeviceTerminal, "mcu", "GPIO5"}, {"", EndpointKind::ConnectorContact, "headers", "GPIO5"}},
        {{"", EndpointKind::DeviceTerminal, "mcu", "GPIO6"}, {"", EndpointKind::ConnectorContact, "headers", "GPIO6"}},
        {{"", EndpointKind::DeviceTerminal, "mcu", "GPIO7"}, {"", EndpointKind::ConnectorContact, "headers", "GPIO7"}},
        {{"", EndpointKind::DeviceTerminal, "mcu", "GPIO8"}, {"", EndpointKind::ConnectorContact, "headers", "GPIO8"}},
        {{"", EndpointKind::DeviceTerminal, "mcu", "GPIO9"}, {"", EndpointKind::ConnectorContact, "headers", "GPIO9"}},
        {{"", EndpointKind::DeviceTerminal, "mcu", "GPIO10"}, {"", EndpointKind::ConnectorContact, "headers", "GPIO10"}},
        {{"", EndpointKind::DeviceTerminal, "mcu", "3V3"}, {"", EndpointKind::ConnectorContact, "headers", "3V3"}},
        {{"", EndpointKind::DeviceTerminal, "mcu", "5V"}, {"", EndpointKind::ConnectorContact, "headers", "5V"}},
        {{"", EndpointKind::DeviceTerminal, "mcu", "GND"}, {"", EndpointKind::ConnectorContact, "headers", "GND"}},
    };
    static const HardwareDefinition definition{
        "esp32-c3-supermini-rev1", AssemblyKind::MarketBoard,
        "ESP32-C3 SuperMini rev1", "ESP32-C3 SuperMini", nullptr,
        {nullptr, 0, devices, 1, connectors, 1, connections,
         countOf(sizeof(connections), sizeof(connections[0]))}};
    return definition;
}

const HardwareDefinition &probeDefinition()
{
    static const Terminal terminals[] = {
        {"VDD", CanonicalNet::V3v3}, {"DATA"}, {"GND", CanonicalNet::Gnd}};
    static const Device devices[] = {
        {"sensor", terminals, 3, "DS18B20", "sensor", "DS18B20"}};
    static const ConnectorContact contacts[] = {
        {"VDD", CanonicalNet::V3v3}, {"DATA"}, {"GND", CanonicalNet::Gnd}};
    static const Connector connectors[] = {
        {"lead", contacts, 3, "Probe lead", ConnectorKind::DirectWire}};
    static const Connection connections[] = {
        {{"", EndpointKind::DeviceTerminal, "sensor", "VDD"}, {"", EndpointKind::ConnectorContact, "lead", "VDD"}},
        {{"", EndpointKind::DeviceTerminal, "sensor", "DATA"}, {"", EndpointKind::ConnectorContact, "lead", "DATA"}},
        {{"", EndpointKind::DeviceTerminal, "sensor", "GND"}, {"", EndpointKind::ConnectorContact, "lead", "GND"}},
    };
    static const HardwareDefinition definition{
        "ds18b20-waterproof-probe", AssemblyKind::SensorProbe,
        "DS18B20 waterproof probe", "DS18B20", nullptr,
        {nullptr, 0, devices, 1, connectors, 1, connections, 3}};
    return definition;
}

const HardwareDefinition &relayDefinition()
{
    static const Terminal terminals[] = {{"CONTROL"}, {"COM"}, {"NO"}, {"NC"}};
    static const Device devices[] = {{"relay", terminals, 4, "Relay", "relay"}};
    static const ConnectorContact control[] = {
        {"VCC", CanonicalNet::V5v}, {"IN"}, {"GND", CanonicalNet::Gnd}};
    static const ConnectorContact load[] = {{"COM"}, {"NO"}, {"NC"}};
    static const Connector connectors[] = {
        {"control", control, 3, "Control", ConnectorKind::Header},
        {"load", load, 3, "Load", ConnectorKind::ScrewTerminal}};
    static const Connection connections[] = {
        {{"", EndpointKind::DeviceTerminal, "relay", "CONTROL"}, {"", EndpointKind::ConnectorContact, "control", "IN"}},
        {{"", EndpointKind::DeviceTerminal, "relay", "COM"}, {"", EndpointKind::ConnectorContact, "load", "COM"}},
        {{"", EndpointKind::DeviceTerminal, "relay", "NO"}, {"", EndpointKind::ConnectorContact, "load", "NO"}},
        {{"", EndpointKind::DeviceTerminal, "relay", "NC"}, {"", EndpointKind::ConnectorContact, "load", "NC"}},
    };
    static const HardwareDefinition definition{
        "generic-relay-module-1ch", AssemblyKind::Module,
        "Generic one-channel relay module", nullptr, nullptr,
        {nullptr, 0, devices, 1, connectors, 2, connections, 4}};
    return definition;
}

const HardwareDefinition &mycroftDefinition()
{
    static const Assembly children[] = {
        {"esp32", "esp32-c3-supermini-rev1", "ESP32 module"}};
    static const Terminal displayTerminals[] = {
        {"CS"}, {"SCK"}, {"MOSI"}, {"3V3", CanonicalNet::V3v3},
        {"GND", CanonicalNet::Gnd}};
    static const Terminal sdTerminals[] = {
        {"CS"}, {"SCK"}, {"MOSI"}, {"MISO"},
        {"3V3", CanonicalNet::V3v3}, {"GND", CanonicalNet::Gnd}};
    static const Device devices[] = {
        {"display", displayTerminals, 5, "Display", "display"},
        {"sd_card", sdTerminals, 6, "SD card", "storage"}};
    static const ConnectorContact socketContacts[] = {
        {"GPIO2"}, {"GPIO3"}, {"GPIO4"}, {"GPIO5"}, {"GPIO6"},
        {"GPIO10"}, {"3V3", CanonicalNet::V3v3}, {"GND", CanonicalNet::Gnd}};
    static const ConnectorContact tempContacts[] = {
        {"VDD", CanonicalNet::V3v3}, {"DATA"}, {"GND", CanonicalNet::Gnd}};
    static const Connector connectors[] = {
        {"mcu_socket", socketContacts, 8, "MCU socket", ConnectorKind::Header},
        {"j_temp", tempContacts, 3, "Temperature probe", ConnectorKind::Jst}};
    static const Connection connections[] = {
        {{"", EndpointKind::ConnectorContact, "mcu_socket", "GPIO2"}, {"esp32", EndpointKind::ConnectorContact, "headers", "GPIO2"}},
        {{"", EndpointKind::ConnectorContact, "mcu_socket", "GPIO3"}, {"esp32", EndpointKind::ConnectorContact, "headers", "GPIO3"}},
        {{"", EndpointKind::ConnectorContact, "mcu_socket", "GPIO4"}, {"esp32", EndpointKind::ConnectorContact, "headers", "GPIO4"}},
        {{"", EndpointKind::ConnectorContact, "mcu_socket", "GPIO5"}, {"esp32", EndpointKind::ConnectorContact, "headers", "GPIO5"}},
        {{"", EndpointKind::ConnectorContact, "mcu_socket", "GPIO6"}, {"esp32", EndpointKind::ConnectorContact, "headers", "GPIO6"}},
        {{"", EndpointKind::ConnectorContact, "mcu_socket", "GPIO10"}, {"esp32", EndpointKind::ConnectorContact, "headers", "GPIO10"}},
        {{"", EndpointKind::ConnectorContact, "mcu_socket", "3V3"}, {"esp32", EndpointKind::ConnectorContact, "headers", "3V3"}},
        {{"", EndpointKind::ConnectorContact, "mcu_socket", "GND"}, {"esp32", EndpointKind::ConnectorContact, "headers", "GND"}},
        {{"", EndpointKind::ConnectorContact, "mcu_socket", "GPIO10"}, {"", EndpointKind::ConnectorContact, "j_temp", "DATA"}},
        {{"", EndpointKind::ConnectorContact, "mcu_socket", "3V3"}, {"", EndpointKind::ConnectorContact, "j_temp", "VDD"}},
        {{"", EndpointKind::ConnectorContact, "mcu_socket", "GND"}, {"", EndpointKind::ConnectorContact, "j_temp", "GND"}},
        {{"", EndpointKind::ConnectorContact, "mcu_socket", "GPIO2"}, {"", EndpointKind::DeviceTerminal, "display", "CS"}},
        {{"", EndpointKind::ConnectorContact, "mcu_socket", "GPIO4"}, {"", EndpointKind::DeviceTerminal, "display", "SCK"}},
        {{"", EndpointKind::ConnectorContact, "mcu_socket", "GPIO6"}, {"", EndpointKind::DeviceTerminal, "display", "MOSI"}},
        {{"", EndpointKind::ConnectorContact, "mcu_socket", "GPIO3"}, {"", EndpointKind::DeviceTerminal, "sd_card", "CS"}},
        {{"", EndpointKind::ConnectorContact, "mcu_socket", "GPIO4"}, {"", EndpointKind::DeviceTerminal, "sd_card", "SCK"}},
        {{"", EndpointKind::ConnectorContact, "mcu_socket", "GPIO6"}, {"", EndpointKind::DeviceTerminal, "sd_card", "MOSI"}},
        {{"", EndpointKind::ConnectorContact, "mcu_socket", "GPIO5"}, {"", EndpointKind::DeviceTerminal, "sd_card", "MISO"}},
    };
    static const HardwareDefinition definition{
        "mycroft-y-controller-rev1", AssemblyKind::CustomBoard,
        "MycroftY controller rev1", "MycroftY", nullptr,
        {children, 1, devices, 2, connectors, 2, connections,
         countOf(sizeof(connections), sizeof(connections[0]))}};
    return definition;
}
}

const HardwareDefinition &esp32C3SuperMiniRev1() { return espDefinition(); }
const HardwareDefinition &mycroftYControllerRev1() { return mycroftDefinition(); }
const HardwareDefinition &ds18b20ProbeDefinition() { return probeDefinition(); }
const HardwareDefinition &genericRelayModule1Ch() { return relayDefinition(); }

const HardwareDefinition *standardDefinitions(size_t &count)
{
    static const HardwareDefinition definitions[] = {
        espDefinition(), mycroftDefinition(), probeDefinition(), relayDefinition()};
    count = sizeof(definitions) / sizeof(definitions[0]);
    return definitions;
}
}
