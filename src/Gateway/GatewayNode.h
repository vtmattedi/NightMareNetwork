#pragma once

#include <Gateway/Broker/Message.h>

// The gateway as a NightMare device: its identity, Configs, Resources and
// command console come from NightMare Network, while its traffic rides the
// gateway's own transport (the ESP-NOW side and the MQTT uplink) instead of
// NightMare's connection stack, which the gateway does not run.
//
// Everything here runs in the gateway tick (its own task, or the caller of
// NightMareGateway_tick()): Resources publish through publishToAll(), which
// walks the device table.
namespace GatewayNode
{
// Injects the gateway transport into the ResourcesManager, then runs
// startNightMareESP(), which binds every Resource declared so far. Called once
// by NightMareGateway_init(), on the task that will tick.
void begin();

// tickNightMareESP(), which also reads the serial console when
// NM_CONSOLE_SERIAL is on.
void tick();

// Offers a message from either side to the gateway's own Resources and
// console. True when the gateway consumed it: it was addressed to the gateway,
// or it is the broker echoing a Resource topic the gateway already delivered.
bool handleMessage(const NightMare::Message &message);
}
