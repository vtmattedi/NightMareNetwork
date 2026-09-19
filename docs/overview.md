---
title: Overview
description: Why NightMare Network exists and how devices, clusters, brokers and backends fit together.
section: overview
order: 1
---

# NightMare Network

NightMare Network gives a device a small, discoverable interface. The device registers Resources once; the Network publishes their schema and current Values, routes writes and Action invocations, and restores publication after reconnect. A consumer learns the interface from the schema instead of knowing the firmware's classes.

## Minimum topology

```text
        MQTT broker
          /     \
      Device A  Device B
```

Each Device can own Resources and mirror Resources owned by another Device. The model also applies to a gateway, desktop application or backend implementation; MQTT is one transport mapping.

## Cluster and remote side

```text
Device A ─┐
Device B ─┼─ Local MQTT ─ Bridge ─ Remote MQTT ─ Backend
Device C ─┘
```

The Local MQTT broker is a cluster coordination bus for low latency device communication. The backend uses the remote broker and does not connect to Local MQTT. A cluster should continue useful local behavior when the remote side is unavailable. Bridges select which traffic crosses that boundary.

A **Cluster** describes local topology. A **Namespace** describes logical addressing. The current implementation uses the default namespace unless an application supplies another name; it does not implement namespace isolation or bridge mapping.

## What the network sees

A **Service** runs application behavior. Its **Resources** form the network interface:

```text
LightController service
  ├─ lightEnabled: Value<bool>, read/write
  └─ toggleLight: Action<void>
```

A generic consumer uses those resources without depending on `LightController`. Sensors and information are read-only Values. Transient notifications are Events. Scheduled work is a Job that can use Resources, rather than another Resource kind.

## Code boundaries

| Folder | Responsibility |
| --- | --- |
| `Core` | System epoch time adapter |
| `Resources` | Model, registry, authority and runtime manager |
| `Network` | Transport, MQTT mapping, dispatcher and console ingress |
| `Runtime` | Scheduler Jobs and manual or managed execution |
| `Services` | Device behavior exposed through Resources |
| `Platform` | ESP32 WiFi and OTA adapters |
| `Legacy/src` | Predecessor implementations kept outside the active build |

[TCP](legacy.md) preceded this resource protocol. The active library uses the Resource model for network-visible behavior.
