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

MQTT owns the MQTT client and its selected `MQTT` or `LOCAL_MQTT` broker
profile. Enabling MQTT requires WiFiIP to be enabled. If WiFiIP has no address,
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
    MQTT,
    LOCAL_MQTT,
    ESP_NOW
};
```

`SelectConnection()` changes and persists the routing preference. It accepts a
compiled transport even when its service is disabled or disconnected. The
current usable active transport stays in place until the preferred transport
becomes usable. Failover considers only enabled, connected services and never
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

bool Mqtt_enable(ConnectionType profile);
bool Mqtt_disable();
bool Mqtt_enabled();
ConnectivityState Mqtt_state();
ConnectionType Mqtt_profile();

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
view: `transport`, `wifi_radio`, `wifi_ip`, `esp_now`, and `mqtt`. Publication
failure does not change connectivity state. Duplicate refresh requests are
coalesced by `SystemState`.
