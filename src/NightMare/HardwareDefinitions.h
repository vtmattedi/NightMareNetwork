#pragma once

#include "HardwareProfile.h"

namespace NMHardware
{
// Small reusable catalog used by examples and available to projects. The
// returned storage has static lifetime. Definitions use the same v2 model as
// custom project hardware; there is no special market-board representation.
const HardwareDefinition &esp32C3SuperMiniRev1();
const HardwareDefinition &mycroftYControllerRev1();
const HardwareDefinition &ds18b20ProbeDefinition();
const HardwareDefinition &genericRelayModule1Ch();

// Returns all four definitions. A profile using MycroftY needs this complete
// set because that definition composes the ESP32-C3 SuperMini definition.
const HardwareDefinition *standardDefinitions(size_t &count);
}
