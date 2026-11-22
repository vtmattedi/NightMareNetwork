#pragma once
#include <Gateway/Broker/Message.h>
#include <cstddef>
#include <cstdint>
#include <string>

// The gateway as a NightMare device. It runs the normal device stack
// (identity, Configs, Resources, console, status, telemetry, scheduler,
// Wi-Fi/SNTP when enabled) on top of NmConnection, whose transport is the
// GATEWAY connection: the gateway's own broker, that is, the ESP-NOW side and
// the MQTT uplink.
//
// Everything here runs in the gateway tick (its own task, or the caller of
// NightMareGateway_tick()).
namespace GatewayNode
{
// Runs startNightMareESP(), which binds every Resource declared so far and
// attaches the GATEWAY connection. Called once by NightMareGateway_init(), on
// the task that will tick.
void begin();
bool started();
// tickNightMareESP(): system publications, scheduler (MANUAL), serial console.
void tick();
// The GATEWAY connection's publish: to every matching ESP-NOW subscriber and
// to the broker. A retained message is kept in the vault, which replays it on
// every (re)connect and to every device that subscribes later, so it is
// accepted even with nobody listening yet.
bool publish(const char *topic, const uint8_t *payload, size_t length, bool retained);
// Offers a message from either side to the device stack. True when the
// gateway consumed it: a request addressed to the gateway itself, or the
// broker echoing something the gateway published and already delivered.
bool handleMessage(const NightMare::Message &message);
}
