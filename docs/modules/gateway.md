---
title: Gateway
description: The NightMare Gateway service -- an ESP-NOW broker bridged to MQTT that is itself a NightMare device.
section: modules
order: 45
---

# Gateway

The NightMare Gateway is an ESP-NOW broker for NightMare devices, bridged to an
MQTT broker. ESP-NOW clients ([ESP-NOW protocol](espnow-protocol.md)) reach the
network through it. It is also a NightMare device in its own right: it has a
device identity, Configs, Resources and a command console.

It is compiled in with `NM_ENABLE_GATEWAY` and lives in `src/Gateway/`.

## Lifecycle

```cpp
#include <NightMare.h>

void setup()
{
    NightMareGatewayConfig config;
    config.espnowConfig.psk = psk;
    config.espnowConfig.pskLength = pskLength;
    config.mqttConfig.host = "broker.local";
    config.radioReady = appRadioReady;
    config.ipReady = appIpReady;
    NightMareGateway_init(config);   // in place of startNightMareESP()
}

void loop()
{
    NightMareGateway_tick();         // in place of tickNightMareESP()
}
```

The gateway owns NightMare's lifecycle. `NightMareGateway_init()` runs
`startNightMareESP()`, and every gateway tick runs `tickNightMareESP()`.
The gateway runs the normal device stack: its NMNW transport is the `GATEWAY`
connection, the gateway's own broker, so status, manifests, Resource states,
`/info`, `/hardware`, telemetry, heartbeat, the scheduler (in the gateway
tick), the Wi-Fi station and SNTP work as on any device. That
keeps Resources, Configs and the ESP-NOW device table on one task: whichever
task ticks the gateway. Bind application Resources before init, as with
`startNightMareESP()`.

`config.tickMode` chooses that task, like the Scheduler's run mode. Its default
comes from `NM_GATEWAY_OWN_TASK`.

```text
TASK    the gateway's own FreeRTOS task, woken by ESP-NOW and MQTT arrivals,
        at most NM_GATEWAY_TICK_MS apart otherwise; NightMareGateway_tick()
        returns at once, so a loop can call it in either mode
MANUAL  only NightMareGateway_tick(), from the application's loop; arrivals
        wait for the next call
```

Application work that touches Resources, such as reading a sensor into a
`ManagedSensor`, belongs in `config.onTick`, which every gateway tick runs.
`config.onLinkChange` reports `NONE`, `LOCAL`, `REMOTE` or `BRIDGED` for a
status indicator.

## Configuration

`NightMareGateway_init()` copies everything it keeps: the PSK and every MQTT
string. It returns `false` when the configuration is unusable.

```text
espnowBroker     run the ESP-NOW side (needs NM_GATEWAY_ESPNOW)
espnowConfig     the network PSK, 16..64 bytes, shared with every device
mqttClient       run the MQTT uplink (needs NM_GATEWAY_MQTT)
mqttConfig       uplink REMOTE | LOCAL, host, port, tls, caCertificate,
                 username, password
radioReady       whether the Wi-Fi driver is started; defaults to WiFiRadio
                 with the library's Wi-Fi compiled in
ipReady          whether the station has an address (mqttClient); defaults
                 to WiFiIP with NM_ENABLE_WIFI
tickMode         TASK | MANUAL
onTick           application work run by every gateway tick
onLinkChange     link status changes
```

With `NM_ENABLE_WIFI` the library's Wi-Fi station runs the gateway's Wi-Fi
(stored profile, `NETWORK WIFI ...` commands, SNTP) and the two callbacks
default to it. Without it, Wi-Fi belongs to the application, which provides
`radioReady` and `ipReady`. ESP-NOW starts once the radio is up; MQTT starts once there is an
address and, with `tls`, a valid clock, since the broker certificate's dates
are checked. `uplink` is what devices read in the gateway announcement
(`uplinks.remote_mqtt` / `uplinks.local_mqtt`).

## Features

```text
NM_ENABLE_GATEWAY         the service                       default 0
NM_GATEWAY_ESPNOW         the local ESP-NOW broker          default NM_ENABLE_GATEWAY
NM_GATEWAY_MQTT           the MQTT uplink                   default NM_ENABLE_GATEWAY
NM_GATEWAY_OWN_TASK       default tickMode: 1 TASK, 0 MANUAL  default 1
NM_GATEWAY_TASK_STACK     own task stack, bytes             default 12288
NM_GATEWAY_TASK_PRIORITY  own task priority                 default 5
NM_GATEWAY_TICK_MS        own task's longest sleep          default 50
```

The gateway requires `NM_ENABLE_RESOURCES`, `NM_ENABLE_CONSOLE` and
`NM_ENABLE_NETWORK`. It refuses three combinations:

- `NM_ENABLE_MQTT`: the uplink is the gateway's own MQTT client.
- `NM_NETWORK_ESPNOW`: a gateway is not also a client.
- `NM_SCHEDULER_OWN_TASK`: jobs run in the gateway tick (`MANUAL`).

## Standard Resources

Every gateway publishes the same quick status and accepts the same controls,
on the normal Resource topics (`<id>/resource/<name>/state`, `/set`):

```text
gateway:clients          sensor int   ESP-NOW sessions
gateway:uplink           sensor bool  MQTT uplink connected
gateway:rx_dropped       sensor int   ESP-NOW frames dropped (cumulative)
gateway:msgs_from_local  sensor int   messages from ESP-NOW devices (cumulative)
gateway:msgs_from_remote sensor int   messages from the broker (cumulative)
gateway:enabled          state  bool  the gateway service on/off
gateway:espnow_paused    state  bool  ESP-NOW side paused (diagnostic)
gateway:hardware_trace   state  bool  per-hop trace of <device>/hardware
```

Sensors refresh every `NM_GATEWAY_STATUS_INTERVAL_MS` (default 5000). The
states follow the gateway whoever changed it (a `/set`, a `GATEWAY` command,
the application). Counters wrap at 2^31; consumers derive rates from two
samples.

## As a NightMare device

The gateway id is the NightMare device name (`gDeviceIdentity`). It is the
generated hardware signature unless a replacement board takes it over with
`ADOPT <address>`; `LABEL SET` gives it a readable name. The address is
locked at init, so an adoption takes effect after a reboot. The same id roots
the gateway's MQTT topics, is its MQTT client id, and is the id in every ESP-NOW
beacon.

The gateway's own Resources and manifests are published through the gateway
itself, to matching ESP-NOW subscribers and to the broker. Retained ones are
kept in the gateway's retained-message vault, which replays them on every MQTT
(re)connect and to every device that subscribes later. Traffic under the
gateway's own name is offered to its `ResourcesManager`. The broker's echo of
the gateway's own Resource topics is consumed rather than routed twice.

The gateway answers NightMare commands on `<id>/console/in` and
`<id>/console/controlled/<n>/in` (answered on the matching `/out`), and on
`all/console/in` (answered on `<id>/console/out` and still routed on). With
`NM_CONSOLE_SERIAL`, the serial console is read by `tickNightMareESP()`, so it
runs in the gateway tick like everything else.

Besides the common built-ins (`ADOPT`, `TIMEZONE`, `CONFIG`, `>` Resource
commands, ...), the gateway adds the `GATEWAY` family (`Gateway/GatewayCommands`)
for its own operation: status, enable/disable, ESP-NOW sessions and pause, the
hardware trace and MQTT diagnostics. See
[GATEWAY commands](../protocols/commands.md#gateway).

## Logging

The gateway logs through NightMare's logger. Network events are `LOG` (info).
Routine detail is `LOG_DEBUG`. Problems are `LOG_WARNING` and `LOG_ERROR`.
NightMare log levels are compile-time, so the per-hop trace of
`<device>/hardware` documents has its own runtime switch,
`NightMareGateway_setHardwareTrace()` (on by default), on top of info level.

## Network contract

A random 64-bit hexadecimal `generation` is created once per boot. Generation
values are opaque equality tokens, not ordered counters.

The frozen retained MQTT topics are:

```text
<gateway-id>/status
<gateway-id>/gateway/network
<gateway-id>/gateway/clients
```

`status` is the normal presence document. It carries the gateway fields
(`schema`, `id`, `kind`, `generation`) and a NightMare device's own (`name`,
the label; `address`; `hardware`; `timezone`), with `online`. The device
stack's own status requests (reconnect, presence reassertion) republish this
document. Its MQTT LWT has `online=false` and
the current generation. `gateway/network` contains schema 1 identity,
capabilities, radio (`channel`, `ssid`, `bssid`) and remote/local MQTT uplink
readiness. `gateway/clients` is a schema 1 projection of `espDeviceManager`;
it does not maintain another liveness table. Each client contains its MAC id,
learned name, connected state, and LastWill topic, byte-array payload, and
retained flag.

On MQTT connection the gateway synchronously publishes its locally-owned
retained vault, current status, network state, and client snapshot before it
subscribes to `#`. Consequently retained broker replay cannot precede the
gateway-owned truth. A retained device status from MQTT is ignored while the
same named device has a live authenticated ESP-NOW session.

The gateway keeps its own `status` and `gateway/network` present for as long as
it is connected. It sees an empty retained publish on either topic (a manual
clear, the backend's "Erase all retained", a broker that lost its store) and
republishes both on its next tick. It also reasserts both every
`GATEWAY_REASSERT_INTERVAL_MS` (default 60000), so a clear it could not observe
is repaired within a minute.

Every bridge also implements the backend recovery projection. It caches
retained client snapshots from other gateways. A matching-generation gateway
LWT applies those LastWills once. Repeated LWT delivery is idempotent, and an
offline event from an older generation cannot invalidate a newer online
generation.

The gateway id in an ESP-NOW beacon is discovery correlation only. The PSK
handshake and encrypted session remain the authentication boundary.

A device gives the gateway a Last Will only while ESP-NOW is its transport. A
`LAST_WILL` frame whose topic length is 0 clears the stored will, so a device
that moved its traffic to MQTT is not marked offline when its ESP-NOW session
later expires.

## Current limits

- A Resource tombstone published while the uplink is down reaches local
  subscribers and the vault but not the broker, so the broker keeps the old
  retained value.
- The application's `OnMessage` on a gateway receives only topics matching its
  `Subscribe()` filters: the gateway's broker carries the whole network.
