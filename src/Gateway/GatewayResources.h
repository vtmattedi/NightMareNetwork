#pragma once

// The gateway's standard Resources: every gateway exposes the same quick
// status and the same controls, on the normal Resource topics, so a dashboard
// or the backend reads and drives any gateway the way it does any device.
//
//   gateway:clients          ManagedSensor<int>   ESP-NOW sessions
//   gateway:uplink           ManagedSensor<bool>  MQTT uplink connected
//   gateway:rx_dropped       ManagedSensor<int>   ESP-NOW frames dropped (cumulative)
//   gateway:msgs_from_local  ManagedSensor<int>   messages from ESP-NOW devices (cumulative)
//   gateway:msgs_from_remote ManagedSensor<int>   messages from the broker (cumulative)
//   gateway:enabled          ManagedState<bool>   the gateway service on/off
//   gateway:espnow_paused    ManagedState<bool>   ESP-NOW side paused (diagnostic)
//   gateway:hardware_trace   ManagedState<bool>   per-hop trace of <device>/hardware
//
// The counters wrap at 2^31; a consumer derives rates from two samples.
namespace GatewayResources
{
// Refreshes the sensors at most every NM_GATEWAY_STATUS_INTERVAL_MS and
// mirrors state changes made by commands. Runs in the gateway tick.
void tick();
}
