---
title: Network connections
description: Lean connection selection, MQTT adaptation, binary-safe messages, and direct ESP WiFi.
section: modules
order: 50
---

# Network connections

`NmConnection` is the connection-neutral boundary used by Resources and other
framework publishers.

```text
ResourcesManager / telemetry / framework
                    │
                    ▼
               NmConnection
                    │
          ┌─────────┴─────────┐
          ▼                   ▼
 MQTT or LOCAL_MQTT       ESP_NOW
  NmMqttConnection       not implemented
          │
          ▼
      NmMqttEsp
```

Connection type and driver implementation are separate concepts. `MQTT` and
`LOCAL_MQTT` both use the existing MQTT implementation, but select different
broker configuration, TLS, credentials, and failure behavior.

## Connection types and states

```cpp
enum class ConnectionType : uint8_t
{
    AUTO = 0,
    MQTT,
    LOCAL_MQTT,
    ESP_NOW
};

enum class ConnectionState : uint8_t
{
    STOPPED,
    DISCOVERING,
    CONNECTING,
    CONNECTED,
    ERROR
};
```

`MQTT` means the Remote MQTT/TLS connection. `LOCAL_MQTT` means the local
broker connection.

`ESP_NOW` is represented but has no driver yet. `AUTO` policy is intentionally
deferred until the concrete connection types are reliable. Selecting either
currently returns `false` without disrupting an active MQTT connection.

## API

```cpp
bool NightMare::Publish(
    const char *topic,
    const uint8_t *payload,
    size_t length,
    bool retained = false);

bool NightMare::Subscribe(const char *topicFilter);
bool NightMare::Unsubscribe(const char *topicFilter);

bool NightMare::SelectConnection(NightMare::ConnectionType connection);

NightMare::ConnectionType NightMare::GetSelectedConnection();
NightMare::ConnectionState NightMare::GetConnectionState();
```

The generic payload boundary is byte pointer plus explicit length. Embedded
zero bytes are therefore preserved when adapted to MQTT, and a future ESP-NOW
driver is not constrained to text payloads.

## Persisted selection

The framework-owned Config is:

```cpp
extern Config<int> NightMare::preferredConnection;
```

Its key is:

```text
nightmare:connection:preferred_connection
```

The accepted connection type is stored as its `ConnectionType` integer. The
default for this first stage is `ConnectionType::MQTT`; `AUTO` is not yet a
policy.

## MQTT reuse

`NmConnection` selects a connection profile and delegates MQTT work without
duplicating the client implementation:

```text
ConnectionType::MQTT
    -> NmMqttConnection::begin(ConnectionType::MQTT)
    -> NmMqttEsp (remote/TLS profile)

ConnectionType::LOCAL_MQTT
    -> NmMqttConnection::begin(ConnectionType::LOCAL_MQTT)
    -> NmMqttEsp (local profile)
```

`NmMqttConnection` owns MQTT adaptation and the reconnect-only MQTT delivery
queue. `NmMqttEsp` owns the ESP-IDF client, broker profiles, TLS, Last Will,
and packet ingress. It reconnects the selected profile but never silently
changes from local to remote or vice versa.

`NmConnection` implements the `ResourcePublisher` and `ResourceSubscriber`
boundary and injects it into `ResourcesManager`.

## Switching

For MQTT connection types, `SelectConnection()` sends a write through
`ConfigManager`. The Config write handler is the single runtime switching
path. It asks the MQTT control task to stop the old client and start the target
profile. On connection, `NmConnection` restores subscriptions once and invokes
the generic connected publication path.

When a switch started from a connected MQTT profile reaches two non-transient
broker connection errors before connecting, `NmConnection` rolls back to the
previous profile and restores that persisted selection. A first-start failure
has no known-good profile and enters `ERROR`. This bounded rollback is separate
from the deferred `AUTO` policy.

No scoring, simultaneous connections, topic-specific routing, message
duplication, or new automatic failover policy is introduced here.

## Connection subscriptions

All subscription intent is stored in one fixed 256-entry registry owned by
`NmConnection`. Console, time synchronization, application, and Resource
subscriptions all enter through `Subscribe()`. The active implementation only
executes the broker operation requested by `NmConnection`.

## Direct ESP WiFi

The active WiFi implementation is under:

```text
src/Network/WiFi/NmWifiEsp.h       ESP-IDF driver, no Arduino
src/Network/WiFi/NmWifiEsp.cpp
src/Network/WiFi/NmWifiService.*   storage, hostname, first-connection services
```

It uses `esp_wifi`, `esp_netif`, and ESP events directly.
While associated, `WiFi_info()` reports the AP signal (`rssi`, dBm) and primary
`channel`; both are 0 when not connected. The driver's mutable state (state, IP,
active profile, TX power, scan results) is guarded by one mutex.

Wi-Fi reports availability to `NmConnection` (`OnLinkAvailabilityIngress`) and
nothing more. `NmConnection` owns starting the preferred connection when the
link comes up, selecting connections and failing over.
