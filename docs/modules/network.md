---
title: Network transports
description: Lean transport selection, MQTT adaptation, binary-safe messages, and direct ESP WiFi.
section: modules
order: 50
---

# Network transports

`NmTransport` is the transport-neutral boundary used by Resources and other
framework publishers.

```text
ResourcesManager / telemetry / framework
                    │
                    ▼
               NmTransport
                    │
          ┌─────────┴─────────┐
          ▼                   ▼
 MQTT or LOCAL_MQTT       ESP_NOW
     NmMqttEsp          not implemented
```

Connection type and driver implementation are separate concepts. `MQTT` and
`LOCAL_MQTT` both use the existing MQTT implementation, but select different
broker configuration, TLS, credentials, and failure behavior.

## Connection types and states

```cpp
enum class TransportType : uint8_t
{
    AUTO = 0,
    MQTT,
    LOCAL_MQTT,
    ESP_NOW
};

enum class TransportState : uint8_t
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

bool NightMare::SelectTransport(NightMare::TransportType transport);

NightMare::TransportType NightMare::GetSelectedTransport();
NightMare::TransportState NightMare::GetTransportState();
```

The generic payload boundary is byte pointer plus explicit length. Embedded
zero bytes are therefore preserved when adapted to MQTT, and a future ESP-NOW
driver is not constrained to text payloads.

## Persisted selection

The framework-owned Config is:

```cpp
extern Config<int> NightMare::preferredTransport;
```

Its key is:

```text
nightmare:connection:preferred_transport
```

The accepted connection type is stored as its `TransportType` integer. The
default for this first stage is `TransportType::MQTT`; `AUTO` is not yet a
policy.

## MQTT reuse

`NmTransport` adapts the existing MQTT implementation rather than duplicating
it:

```text
TransportType::MQTT
    -> MQTT_Init(false) / NmMqttEsp

TransportType::LOCAL_MQTT
    -> MQTT_Init(true) / NmMqttEsp
```

The existing MQTT queue, discovery, project callbacks, routing, reconnect
publication, Last Will, and broker-error behavior remain in `MQTT.cpp` and
`NmMqttEsp.cpp`.

Only the old `MqttResourceTransport` adapter was removed. `NmTransport` now
implements the `ResourcePublisher` and `ResourceSubscriber` boundary and
injects it into `ResourcesManager`. On MQTT reconnect, existing MQTT lifecycle
code still asks `ResourcesManager` to restore exact subscriptions and announce
framework/Resource state.

## Switching

For MQTT connection types, `SelectTransport()` persists the enum value,
updates the selected type, and asks the existing MQTT control task to switch
the complete connection. The MQTT task stops the old client before starting
the target client. The normal connected path then restores subscriptions and
requests state publication.

No scoring, simultaneous transports, topic-specific routing, message
duplication, or new automatic failover policy is introduced here.

## Transport subscriptions

Transport-level subscription intent is stored in a fixed 16-entry table and
restored when MQTT reconnects. Resource subscriptions are managed separately
by `ResourcesManager` and do not consume these application subscription slots.

## Direct ESP WiFi

The active WiFi implementation is under:

```text
src/Network/WiFi/NmWifiEsp.h
src/Network/WiFi/NmWifiEsp.cpp
```

It uses `esp_wifi`, `esp_netif`, and ESP events directly. The old
`Plataform/ESP32/NightMareWIFI.*` path remains as a compatibility shim.
