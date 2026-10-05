---
title: Network
description: Independent Wi-Fi radio, Wi-Fi IP, MQTT, and ESP-NOW services with connection-neutral NMNW routing.
section: modules
order: 40
---

# Network

NightMare separates connectivity lifecycle from NMNW transport routing.

```text
WiFiRadio
 ├─ WiFiIP
 │   └─ MQTT
 └─ ESP-NOW
```

`supported` means the service was compiled into the firmware, `enabled` means
the service has been requested to operate, and `state` reports runtime truth.
Every service uses `ConnectivityState`: `STOPPED`, `STARTING`, `READY`,
`CONNECTING`, `CONNECTED`, `SUSPENDED`, or `ERROR`. `enabled` never means
connected. The radio normally reports `enabled=true, state=READY`.

## Lifecycle boundaries

WiFiRadio owns the ESP Wi-Fi driver. It starts during framework startup when
WiFiIP or ESP-NOW is compiled and does not join an AP. Normal commands do not
turn it off.

WiFiIP owns STA association, DHCP/IP, reconnect, the stored profile, RSSI,
channel, TX power, and scans. Enabling WiFiIP requires a ready radio. Disabling
it leaves the radio and ESP-NOW alone, but is denied while MQTT is enabled.

MQTT owns one MQTT client and its selected `REMOTE` or `LOCAL` broker profile.
The profile and its internal broker fallback are MQTT policy, not separate
NMNW transports. Enabling MQTT requires WiFiIP to be enabled. If WiFiIP has no address,
MQTT remains enabled and waits; it never enables WiFiIP. Disabling MQTT does
not disable WiFiIP.

ESP-NOW owns gateway discovery and its secure session. It requires a ready
radio but neither enables nor disables WiFiIP or MQTT. WiFiIP and ESP-NOW may
therefore run together. While a station is associated, the AP owns the radio
channel and ESP-NOW uses that channel; without an association, ESP-NOW may hop.

These rules are strict: selecting a transport does not start or stop any
connectivity service.

## NMNW transport routing

`NmConnection` owns only the preferred transport, the one active usable
transport, failover routing, generic publication, and the shared subscription
registry. `ConnectionType` remains the NMNW transport enum:

```cpp
enum class ConnectionType : uint8_t
{
    AUTO = 0,
    MQTT = 1,
    ESP_NOW = 3
};
```

Numeric value 2 is reserved for the removed pre-freeze `LOCAL_MQTT` transport
and is not reused. `MqttProfile::{REMOTE, LOCAL}` selects the one MQTT client's
broker policy.

`SelectConnection()` changes and persists the routing preference. It accepts a
compiled transport even when its service is disabled or disconnected. The
current usable active transport stays in place. ESP-NOW may replace healthy
MQTT only after a probable gateway announcement satisfies the required uplink
and radio compatibility, and that same gateway authenticates and connects.
Failover considers only eligible, enabled, connected services and never
calls a service enable/disable function.

Only the active transport carries normal framework traffic. When it changes,
NightMare removes the shared subscriptions from the previous transport,
restores them on the new one, and runs the normal reconnect publication path.
The framework does not duplicate publications over MQTT and ESP-NOW.

## Public lifecycle API

```cpp
bool WiFiIP_enable();
bool WiFiIP_disable();
bool WiFiIP_enabled();
ConnectivityState WiFiIP_state();
WiFiInfo WiFi_info();

bool Mqtt_enable(MqttProfile profile);
bool Mqtt_disable();
bool Mqtt_enabled();
ConnectivityState Mqtt_state();
MqttProfile Mqtt_profile();

bool EspNow_enable();
bool EspNow_disable();
bool EspNow_enabled();
ConnectivityState EspNow_state();
bool EspNow_suspend(ConnectivitySuspendReason reason);
bool EspNow_resume(ConnectivitySuspendReason reason);
```

Service enable state is runtime-only in this architecture pass. WiFiIP follows
`NM_WIFI_AUTO`; compiled MQTT and ESP-NOW services start independently when
their dependencies are satisfied. The preferred transport is not used as an
enable policy.

## Scanning and channel coexistence

`NETWORK WIFI SCAN` is allowed while ESP-NOW is enabled or connected. The scan
path suspends ESP-NOW with `ConnectivitySuspendReason::WIFI_SCAN`, starts the
asynchronous ESP-IDF scan, and resumes ESP-NOW through one finalization path on
completion, start failure, abort, or timeout. During suspension ESP-NOW remains
enabled and reports `SUSPENDED`. WiFiIP, MQTT, and transport preference are not
changed.

If WiFiIP is associated, ESP-IDF returns the radio to the AP-owned channel
after the scan. ESP-NOW then searches or reconnects on that channel. It never
disables WiFiIP to recover a gateway session.

## State notifications and telemetry

Drivers report facts to their owning service. The service updates its own
state, requests a network telemetry refresh, and asks `NmConnection` to
reevaluate routing. No callback starts or stops an unrelated service.

`<device>/telemetry/network` and `NETWORK GET` expose the same independent
view: `transport`, `wifi_radio`, `wifi_ip`, `esp_now`, `mqtt`, and
`gateway_candidate`. Candidate observation never initiates discovery. Publication
failure does not change connectivity state. Duplicate refresh requests are
coalesced by `SystemState`.

## Gateway candidate and sticky fallback

NMNW subscribes to retained `+/gateway/network` announcements. A candidate is
probable only when it is online, ESP-NOW-ready, ready for the selected MQTT
route class (`REMOTE` or `LOCAL`), and radio-compatible. BSSID equality is
preferred; otherwise SSID and channel must match. The announcement is only
readiness evidence. The ESP-NOW PSK handshake authenticates the gateway, and
the beacon's stable gateway id must match the announcement before routing may
switch from healthy MQTT.

Reachable, eligible, preferred, and active are separate facts. A healthy MQTT
fallback remains active while no probable gateway exists, during an ESP-NOW
attempt, and after a failed attempt. If the active transport is lost, routing
walks the normal preferred/fallback queue; `preferred != active` is expected.

## Frozen gateway MQTT contract

A gateway uses one stable id in MQTT and its ESP-NOW beacon, plus a boot-unique
opaque `generation`. These retained topics and schema-1 document shapes are
frozen for the network contract:

```text
<gateway-id>/status
{"schema":1,"device":"<gateway-id>","id":"<gateway-id>","kind":"gateway","online":true,"generation":"<opaque>"}

<gateway-id>/gateway/network
{"schema":1,"device":"<gateway-id>","id":"<gateway-id>","kind":"gateway","online":true,"generation":"<opaque>","capabilities":{"esp_now":true,"mqtt_bridge_remote":true,"mqtt_bridge_local":false},"radio":{"channel":6,"ssid":"example","bssid":"00:11:22:33:44:55"},"uplinks":{"remote_mqtt":{"ready":true},"local_mqtt":{"ready":false}}}

<gateway-id>/gateway/clients
{"schema":1,"id":"<gateway-id>","generation":"<opaque>","clients":[{"id":"aa:bb:cc:dd:ee:ff","name":"device-name","connected":true,"last_will":{"topic":"device-name/status","retained":true,"payload":[123,34,111,110,108,105,110,101,34,58,102,97,108,115,101,125]}}]}
```

The gateway MQTT LWT is its `status` document with `online=false` and the
current generation. On reconnect it publishes locally owned retained state,
then gateway status/network/client state, and only then subscribes for broker
replay. A recovery consumer applies the cached LastWills of connected clients
once for a matching-generation gateway offline event. Repeated delivery is
idempotent, and an older-generation offline event cannot invalidate a newer
online generation. LastWill payload is an exact byte array; consumers must not
reinterpret it as text.
