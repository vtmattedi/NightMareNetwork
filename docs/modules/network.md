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

`AUTO` is the failover order with no preferred profile on top; see
[Failover](#failover).

`ESP_NOW` (built when `NM_NETWORK_ESPNOW` is set, ESP-IDF only) connects to the
Nightmare Gateway through `Network/EspNow/EspNowClient.*`. It runs a
PSK-authenticated handshake, gets a gateway-assigned session id, and switches
the link to ESP-NOW encryption with a per-session key. It keeps the session
alive with the gateway's heartbeat and re-sends subscriptions and the last will
after every new session. It needs `NM_ESPNOW_PSK` in `creds.h`. The wire
contract is in [espnow-protocol.md](espnow-protocol.md).

## API

```cpp
bool NightMare::Publish(
    const char *topic,
    const uint8_t *payload,
    size_t length,
    bool retained = false);

using MessageHandler =
    void (*)(const char *topic,
             const uint8_t *payload,
             size_t length,
             bool retained);

void NightMare::OnMessage(MessageHandler handler);

bool NightMare::Subscribe(const char *topicFilter);
bool NightMare::Unsubscribe(const char *topicFilter);

bool NightMare::SelectConnection(NightMare::ConnectionType connection);

NightMare::ConnectionType NightMare::GetSelectedConnection();
NightMare::ConnectionState NightMare::GetConnectionState();
```

The generic payload boundary is byte pointer plus explicit length. Embedded
zero bytes are therefore preserved by both MQTT and ESP-NOW.

`OnMessage()` installs one application handler, replacing the previous handler;
passing `nullptr` removes it. After Resource, time, console, and other enabled
framework routes decline a message, the router invokes this handler with the
full topic and the retained flag supplied by the active connection. Framework
messages do not also reach the application handler.

The callback runs synchronously on the active connection's ingress context.
The topic and payload pointers are valid only for the callback duration, so the
application must copy data it needs afterward and should keep the callback
short.

```cpp
void onMessage(const char *topic, const uint8_t *payload,
               size_t length, bool retained)
{
    // Handle custom subscribed traffic.
}

void setup()
{
    NightMare::OnMessage(onMessage);
    NightMare::Subscribe("test/+");
}
```

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
build default is the first enabled of `MQTT`, `LOCAL_MQTT`, `ESP_NOW`. `AUTO`
(`0`) is accepted and means "no preference": the base failover order.

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

An explicit selection restarts the failover order from its new head. A
profile that reaches two non-transient broker connection errors enters
`ERROR`; what runs next is decided by failover, not by the driver.

## Failover

One connection runs at a time. The failover order is the base order with the
preferred profile moved to the top; profiles the build lacks are left out:

```text
base (AUTO)          ESP_NOW, MQTT, LOCAL_MQTT
preferred LOCAL_MQTT LOCAL_MQTT, ESP_NOW, MQTT
preferred MQTT       MQTT, ESP_NOW, LOCAL_MQTT
```

`ConnectionTick()`, called from `tickNightMareESP()`, starts the next runnable
profile in that order when the active one has been down for
`nightmare:connection:failover_secs` (default `60`, range `0`-`86400`), or at
once when it is in `ERROR`. "Down" covers never connecting, being
disconnected, and a head that is still waiting for its radio or IP link. A
profile whose requirement is not met is skipped; wrapping round to the active
profile restarts it. Switches are at least 5 s apart, and each newly started
profile gets a full window. `0` disables failover: the selected profile keeps
retrying on its own.

Failover never rewrites `preferred_connection`, so the next boot starts from
the preferred profile again. It does not return to a higher-priority profile
while the current one stays connected; it only moves on when the current one
goes down.

No scoring, simultaneous connections, topic-specific routing or message
duplication is introduced here.

## Connection subscriptions

All subscription intent is stored in one fixed 256-entry registry owned by
`NmConnection`. Console, time synchronization, application, and Resource
subscriptions all enter through `Subscribe()`. The active implementation only
executes the broker operation requested by `NmConnection`.

## Direct ESP WiFi: radio and IP station

Wi-Fi is two layers, because "the radio is started" and "the station has an IP"
are different capabilities -- ESP-NOW needs the first and not the second:

```text
src/Network/WiFiRadio/NmWifiRadio.*         ESP-IDF: driver init, STA mode, esp_wifi_start
src/Network/WiFiRadio/NmWifiRadioService.*  reports the radio; stops the station with it
src/Network/WiFiIP/NmWifiEsp.*              ESP-IDF: join an AP, get an IP, reconnect, scan
src/Network/WiFiIP/NmWifiService.*          storage, hostname, first-connection services
```

`NM_ENABLE_WIFI_RADIO` is derived (`NM_ENABLE_WIFI || NM_NETWORK_ESPNOW`); a
radio-only ESP-NOW build sets `NM_ENABLE_WIFI 0` and never joins an AP. The
radio initialises the driver without driver NVS (`nvs_enable = 0`), so a stale
saved SSID cannot make it look like a station owns the channel.

Both use `esp_wifi`, `esp_netif`, and ESP events directly.
While associated, `WiFi_info()` reports the AP signal (`rssi`, dBm) and primary
`channel`; both are 0 when not connected. The station's mutable state (state, IP,
active profile, TX power, scan results) is guarded by one mutex.

Each layer reports availability to `NmConnection` and nothing more:

```text
OnRadioAvailabilityIngress    radio started/stopped    ESP_NOW needs this
OnIpLinkAvailabilityIngress   station has an IP / not  MQTT, LOCAL_MQTT need this
```

`NmConnection` owns starting the preferred connection once what it runs on is
available, selecting connections and failing over. A preferred connection that
is supported but not ready yet is waited for rather than skipped, so with MQTT
preferred the radio coming up first does not start ESP-NOW instead. The wait
counts as down time: after `failover_secs` the next runnable profile starts.
