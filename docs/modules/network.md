---
title: Network and MQTT
description: MQTT transport, broker switching, subscriptions, routing, discovery, and project hooks.
section: modules
order: 50
---

# Network and MQTT

NightMare's MQTT implementation is intentionally layered.

```text
application
    │
    ▼
MQTT.cpp
    project-facing facade
    │
    ▼
NmMessageRouter
    NightMare protocol routing
    │
    ▼
NmMqttEsp
    ESP MQTT client + broker lifecycle
```

`ResourcesManager` connects to the facade through small publisher/subscriber interfaces rather than owning the MQTT client directly.

## MQTT facade

The project-facing API is in:

```text
Network/MQTT.h
```

It exposes:

```cpp
MQTT_Init(...)
MQTT_End()
MQTT_Finish()
MQTT_change_to(...)

MQTT_isLocal()
MQTT_Connected()
MQTT_State()
MQTTStateJson()

MQTT_Publish(...)
MQTT_Queue_Async_Message(...)

MQTT_SubscribeTopic(...)
MQTT_UnsubscribeTopic(...)

MQTT_SetDiscovery(...)
MQTT_DiscoveryEnabled()

MQTT_onMessage(...)
MQTT_onConnected(...)
MQTT_onDisconnected(...)
```

Applications normally use this layer rather than `NmMqttEsp` directly.

## Local vs Remote MQTT

The public convenience constants are:

```cpp
LOCAL_MQTT  == true
REMOTE_MQTT == false
```

The boolean selects a broker transport.

It is unrelated to Resource ownership.

A Remote Resource may be reached through the Local MQTT broker, and a Managed Resource may be published while connected to Remote MQTT.

Those are different concepts.

## Broker URIs

The current ESP transport builds:

```text
local:
mqtt://<LOCAL_MQTT_HOST>:<LOCAL_MQTT_PORT>

remote:
mqtts://<REMOTE_MQTT_URL>:<REMOTE_MQTT_PORT>
```

Remote MQTT uses TLS verification with:

```cpp
ROOT_CA
```

from the consuming project's `creds.h`.

The local broker currently uses plain MQTT rather than TLS.

## Credentials

The current MQTT transport expects `creds.h` to provide the MQTT definitions, including:

```text
LOCAL_MQTT_HOST
LOCAL_MQTT_PORT
REMOTE_MQTT_URL
REMOTE_MQTT_PORT
MQTT_USER
MQTT_PASSWD
ROOT_CA
```

`ROOT_CA` is mandatory in the current build whenever MQTT is enabled because the remote broker path is compiled in.

## Client ID

The ESP MQTT client ID is generated as:

```text
nm-<deviceId>
```

It uses the raw hardware device ID rather than the adoptable logical device name.

Adopting a new logical device name therefore does not change the MQTT client ID.

## Last Will

The transport installs a retained QoS-0 Last Will at:

```text
<device>/status
```

with the normal offline status JSON.

Graceful client shutdown publishes the same offline JSON before stopping.

This keeps graceful and ungraceful presence payloads consistent.

## MQTT state

```cpp
MQTT_State();
```

uses current implementation state codes:

```text
-2  connecting
-1  stopped / not initialized
 0  disconnected
 1  connected to LAN/local broker
 2  connected to remote/cloud broker
```

Use:

```cpp
MQTT_Connected();
```

when only connected/not-connected matters.

Use:

```cpp
MQTT_isLocal();
```

to query the currently selected broker.

## Starting MQTT

Direct startup:

```cpp
MQTT_Init(REMOTE_MQTT);
```

or:

```cpp
MQTT_Init(LOCAL_MQTT);
```

`MQTT_Init()`:

- initializes/locks device identity,
- injects MQTT transport into `ResourcesManager`,
- installs transport handlers,
- asks `NmMqttEsp` to start the selected broker.

In the standard `startNightMareESP()` lifecycle, MQTT is not started directly there.

`startNightMareESP()` starts WiFi, and the first successful WiFi connection calls:

```cpp
MQTT_Init(false);
```

which selects Remote MQTT initially.

## Stop vs finish

```cpp
MQTT_End();
```

requests that the current MQTT client stop while keeping the MQTT control task available for later reuse.

```cpp
MQTT_Finish();
```

requests asynchronous shutdown of the MQTT client and control task and detaches MQTT transport from `ResourcesManager`.

After finish completes, `MQTT_Init()` can create the transport again.

## Broker switching

```cpp
MQTT_change_to(LOCAL_MQTT);
MQTT_change_to(REMOTE_MQTT);
```

queues a broker switch in the MQTT control task.

The old client is stopped before a new client is created.

## Error-driven broker fallback

The current ESP transport tracks MQTT transport errors.

After repeated broker errors it queues a switch:

```text
local -> remote
remote -> local
```

The current threshold constant is:

```text
MaxBrokerErrors = 1
```

and the switch occurs once the incremented error count is greater than that value.

This behavior belongs to the current transport implementation rather than the Resource model.

## Publishing

```cpp
bool MQTT_Publish(
    const String &topic,
    const String &message,
    bool insertOwner = true,
    bool retained = false);
```

With default `insertOwner = true`:

```cpp
MQTT_Publish("custom/state", "1");
```

publishes to:

```text
<device>/custom/state
```

With:

```cpp
insertOwner = false
```

the supplied topic is used exactly.

An empty retained payload deletes a retained topic according to normal MQTT semantics.

## QoS

The current transport publishes and subscribes at:

```text
QoS 0
```

NightMare does not currently expose per-message QoS configuration through the public MQTT facade.

## Asynchronous queue

```cpp
MQTT_Queue_Async_Message(...)
```

first attempts immediate publication.

If publication cannot be accepted, it can store the message in a small in-memory queue.

Current queue capacity:

```text
5 messages
```

Queued messages retain:

```text
full topic
payload
retained flag
```

and are flushed after MQTT connects.

The queue is runtime-only and is not persistent storage.

## Incoming payload limit

The ESP MQTT transport assembles fragmented incoming MQTT data into a complete payload.

Current maximum:

```text
32768 bytes
```

Oversized or inconsistent fragmented messages are dropped before they reach the NightMare router.

Individual protocols may impose smaller limits, such as the Resource 2048-byte payload cap.

## Automatic subscriptions

On every MQTT connection, the facade restores:

- device console topics,
- controlled-console topic,
- broadcast console topic,
- global time-sync topic when enabled,
- exact subscriptions required by bound Remote/Managed Resources,
- optional discovery topics,
- project custom subscriptions.

Applications do not need to re-register them after reconnect.

## Routing order

Incoming MQTT is processed as:

```text
NmMqttEsp
    │
    ▼
MQTT facade
    │
    ▼
NmMessageRouter
    │
    ├── ResourcesManager
    ├── Control/time
    ├── console
    └── controlled console
    │
    ▼
project callback if unconsumed
```

Recognized Resource traffic is considered consumed even if decoding or application execution later rejects the operation.

This prevents known protocol traffic from leaking into the application's generic callback.

## Project message callback

Register:

```cpp
MQTT_onMessage(callback);
```

Default behavior:

```text
onlyDeviceMessages = true
```

The callback receives only unconsumed topics under the current device root, with that root removed.

Example:

```text
MQTT topic:
bedroom-ac/custom/data

callback topic:
custom/data
```

To receive unconsumed external/full topics:

```cpp
MQTT_onMessage(callback, false);
```

Then the callback receives the complete topic String.

## Connected callback

```cpp
MQTT_onConnected(callback);
```

runs after subscriptions are restored and standard publications are requested.

Before the project callback, the framework has already:

- restored subscriptions,
- requested online status,
- requested Resource state and provider/consume manifest re-announcement,
- requested INFO and the hardware configuration document when telemetry is enabled,
- flushed queued messages.

The requested publications are completed cooperatively by later
`tickNightMareESP()` calls, one request per tick. Applications should therefore
keep calling the standard tick after connection.

## Disconnected callback

```cpp
MQTT_onDisconnected(callback);
```

receives:

```cpp
bool localBroker
```

describing which broker disconnected.

## Reconnect publication

`NmMessageRouter::onConnected()` requests deferred publication of:

```text
status
Resource manifest
consume manifest
Managed Value state
INFO
hardware configuration
console connection message
time request when wall time is invalid
```

The console and time-request messages remain small immediate publications. The
typed pending requests are consumed by `tickNightMareESP()`. A failed request
moves behind other ready work and retries with exponential backoff capped at
five minutes. A fresh request resets its backoff. No retry is attempted while
offline; an elapsed delay is ready after reconnect.

## Custom subscriptions

Register:

```cpp
MQTT_SubscribeTopic("external/device/state");
```

Remove:

```cpp
MQTT_UnsubscribeTopic("external/device/state");
```

Current capacity:

```text
16 custom filters
```

Maximum filter length:

```text
192 characters
```

Custom filters are kept in RAM and restored after reconnect.

Wildcard placement for `+` and `#` is validated.

## Discovery

Enable:

```cpp
MQTT_SetDiscovery(true);
```

NightMare subscribes to:

```text
+/manifest
+/status
```

Manifest traffic can be delivered through:

```cpp
gResourcesManager.setManifestHandler(...)
```

Status is ordinary unconsumed MQTT traffic and can reach:

```cpp
MQTT_onMessage(..., false);
```

Discovery is optional and separate from exact Remote Resource subscriptions.

## ResourcesManager transport boundary

`ResourcesManager` does not depend directly on the ESP MQTT client.

It talks through:

```cpp
ResourcePublisher
ResourceSubscriber
```

The MQTT facade supplies an adapter implementing those interfaces.

This keeps Resource routing independent of `esp_mqtt_client` details.

## What belongs where

Use the Resource API for application state/capabilities.

Use the MQTT facade for:

- custom non-Resource topics,
- broker state/control,
- optional discovery,
- project-level unconsumed MQTT traffic.

Use `NmMqttEsp` only when working on NightMare's transport implementation itself.
