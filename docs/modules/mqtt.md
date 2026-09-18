---
title: MQTT
description: The MQTT client — topic prefixing, retained status and last-will, the console hooks, chunked publishing, and the one threading rule its callback imposes.
section: modules
order: 10
---

# MQTT — `Core/MQTT.h`

A wrapper over Espressif's `esp-mqtt` (`mqtt_client.h`). It replaced
PubSubClient after a run of crashes, and it is where most of the wire contract
in [Topics](/docs/protocols/topics) is implemented. Compiled with
`COMPILE_MQTT`; needs `creds.h` for the broker settings.

## Starting it

```cpp
void onWifiConnected(bool first) { if (first) MQTT_Init(REMOTE_MQTT); }
```

`MQTT_Init(bool local)` picks the broker: `REMOTE_MQTT` (`false`) is the TLS
cloud broker from `creds.h` (`REMOTE_MQTT_URL`, `REMOTE_MQTT_PORT`, `ROOT_CA`),
`LOCAL_MQTT` (`true`) the plain one on the LAN (`LOCAL_MQTT_HOST`,
`LOCAL_MQTT_PORT`). Both share `MQTT_USER`/`MQTT_PASSWD`.
`MQTT_change_to(local)` swaps at runtime; `MQTT SWAP` on the console does the
same. `MQTT_isLocal()` says which is active.

The client id is `<DeviceName>-<random hex>`, so two boots never collide.

### What connecting does

1. Subscribes to `#`.
2. Publishes `<Device>/status` = `online`, **retained** — which also clears a
   stale retained `offline` from the last-will.
3. Publishes `Booted` (first connection) or `Connected` on `console/out`.
4. Flushes anything queued with `MQTT_Queue_Async_Message()` while offline.

The last-will is `<Device>/status` = `offline`, retained, QoS 0, set at init.

`MQTT_DISABLE_BROKER_FAILOVER` in `Modules.config.h` turns off the automatic
fall-back between brokers. The Dashboard sets it: with no local broker
present, a cloud hiccup rebuilt the client against the missing local one, timed
out, rebuilt it back, and churned the heap forever.

## Publishing

```cpp
void MQTT_Send(String topic, String message, bool insertOwner = true, bool retained = false);
void MQTT_Send_Raw(String topic, String message);
bool MQTT_Queue_Async_Message(String topic, String message, bool insertOwner = false, bool retained = false);
bool MQTT_ClearRetained(String topic);   // empty retained publish: the broker forgets the topic
```

- With `insertOwner` (the default) a leading `/` is stripped and
  `<DeviceName>/` is prepended: `MQTT_Send("/sensors", …)` and
  `MQTT_Send("sensors", …)` both go to `Adler/sensors`.
- `insertOwner = false` sends to the topic as given — how a device addresses
  *another* device's console: `MQTT_Send("Adler/console/in", "POWER 0", false)`.
- `MQTT_Send_Raw` never prefixes. Use it for shared channels the device does
  not own, like `Control/request`.
- Messages over `MQTT_MAX_CHUNK_SIZE` (512 B) are split and each chunk
  prefixed `;;n/total;;` — the [MQTTP](/docs/protocols/mqttp) chunk format.
- **Empty payloads are dropped**, so a command that returns nothing is
  indistinguishable from a timeout at the receiver.
- `MQTT_SKIP_PUBLISH_IF_DISCONNECTED` is defined: a publish while offline is
  discarded, not queued. `MQTT_Queue_Async_Message` is the explicit queue
  (`MAX_ASYNC_QUEUE_MESSAGES` = 5), flushed on the next connect.

## Receiving

```cpp
void MQTT_onMessage(void (*cb)(String topic, String message), bool onlyDeviceMessages = true);
void MQTT_onConnected(void (*cb)(void));
void MQTT_onDisconnected(void (*cb)(bool));
```

Before your callback sees anything, the client handles its own topics:

| topic | handled as |
| --- | --- |
| `<Device>/console/in`, `all/console/in` | command → resolver → reply on `console/out` (with `MQTT_PREPROCESS`) |
| `<Device>/console/controlled/<id>/in` | MQTTP request → reply on `…/<id>/out` |
| `Control/time` | clock sync, consumed |

With `onlyDeviceMessages = true` the callback receives only topics under
`<Device>/`, **with that prefix stripped** — `light`, not `Adler/light`. With
`false` it receives everything on the broker, prefix intact; that is what a
device that listens to other devices' `sensors` needs.

### The threading rule

**The callback runs on the esp-mqtt task, not on `loop()`.** On a dual-core
ESP32 that is true parallelism; on a C3 it is preemption. Application state
touched from the callback and from `loop()` at the same time corrupts the
heap — two tasks reallocating the same `String` was the Dashboard's
"exhausted after two minutes" crash.

The rule that follows: the callback classifies by topic shape without
allocating, copies what it wants into a fixed ring buffer and returns.
`loop()` drains the buffer and does the work. Drop and count when the buffer is
full; a callback that blocks makes the client miss keep-alives and the broker
drops the connection, which is worse than a lost reading.

The library does not enforce this. Its enforcement is the reason the design
document proposes lifting the Dashboard's inbox into `Core/`.

## State

```cpp
bool   MQTT_Connected();
int8_t MQTT_State();        // esp-mqtt client state
String MQTTStateJson();     // what "MQTT STATE" on the console returns
```

## `Send_to_MQTT`

```cpp
void Send_to_MQTT(String topic, String message);
```

The hook the [Services](/docs/modules/services) layer publishes through, and
that `ServicesCore.h` declares `extern`. It is `MQTT_Send(topic, message)` with
the defaults, provided by this module so the Services do not depend on the
MQTT header directly.
