---
title: Architecture
description: Dependency boundaries between the Resource model, runtime manager and transport.
section: architecture
order: 1
---

# Architecture

The active library follows one path from a device's capabilities to the network:

```text
Service or sensor driver
      ↓
NetResource model → ResourceRegistry → ResourceManager
                                         ↓
                                      Dispatcher
                                         ↓
                                   MqttTransport
```

A Resource has a stable ID, compact kind and type enums, access, and an optional pointer to caller-owned metadata. It has no MQTT topic, callback, task or manager pointer. `NetValue<T>` owns its current value. `NetAction<Args>` describes an invocation and `NetEvent<Payload>` describes a transient occurrence. Another language can implement the same identity and operation model without adopting the ESP32 runtime.

The Registry records what exists and whether each resource is locally authoritative or mirrors another device. The Manager stores handlers and publication policy in fixed slots. `Network::tick` drains MQTT messages on the Runtime thread, then the Dispatcher routes each operation to the Manager. This prevents application code from running inside the MQTT callback.

## Authority

A local Value can be changed with `resources.set(value, actual)`. That changes the object and publishes authoritative state when the policy calls for it. `value.set(actual)` changes the object alone. A remote mirrored Value receives state updates without publishing them back. A writable remote mirror may send `resources.request(value, desired)`; that request does not change its local state until the owner publishes the result.

A write sent to a local Value calls its registered write handler. The handler validates and applies the request, then calls `resources.set` with the actual state. It may reject the request or clamp it. Access and role are independent.

## Registration and reconnect

`resources.add(resource, policy)` makes a local resource discoverable and publishable. `resources.mirror(resource, owner)` binds a remote object to another device. On connection the Manager publishes every local schema and current Value. If registration happens while connected, it publishes immediately. Periodic publication is driven by one `ResourceManager::tick` pass, not a timer per Value.

The Network subscribes to the resource wildcard, queues bounded inbound messages and triggers the Manager's reconnect behavior. Events are not replayed. Only current local Values are republished.

## Execution and clocks

Runtime calls registered pollers and the Scheduler from `loop()` or one optional managed FreeRTOS task. Jobs are logical scheduled work, not Resource kinds and not FreeRTOS tasks. Interval Jobs use monotonic `millis()`; daily Jobs use the system epoch clock and wait until it is valid. [Runtime and time](runtime.md) gives the details.

## Historical code

The former `ServerVariable`, separate Timers/Scheduler, command resolver, TCP, HTTP and controller proxies are kept under `Legacy/src`. They are outside the active library build. [Legacy](legacy.md) records what their useful behavior became.
