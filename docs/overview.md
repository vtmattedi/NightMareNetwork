---
title: Overview
description: What NightMare Network is, what a device gets from the library for free, and the pieces around it.
section: overview
order: 1
---

# NightMare Network

NightMare Network is a C++ library for ESP32 devices on a home network, and the
conventions those devices share so that a wall panel, a backend and an AI
assistant can all talk to them the same way.

It started as the code that kept being copied between projects — WiFi bring-up,
an MQTT client that survives broker failover, a command console, timers, a
scheduler, persistent config — and became the thing every device is built on.
Today it runs an air conditioner controller, a headless light-and-door node, a
wall dashboard, and the devices those talk to.

## The shape of it

```
                 NightMare Network repository
                           │
                ┌──────────┴──────────┐
                │                     │
             src/  (C++)          docs/  (Markdown)
                │                     │
          PlatformIO            ┌─────┴─────┐
                │               ▼           ▼
             ESP32           Website       MCP
                                │           │
                              Humans        AI
```

One repository holds the library, its documentation, the website that renders
that documentation, and the MCP server that hands the same documentation to an
AI assistant. A change to a module and the paragraph describing it land in the
same commit.

## What a device gets by linking the library

Add one line to `platformio.ini` and supply two headers, and a device has:

| capability | what it means on the wire |
| --- | --- |
| **Identity** | `<Device>/status` = `online`, retained, on every connect; a retained `offline` last-will when it drops. |
| **Liveness** | `<Device>/telemetry` — uptime, heap, RSSI, IP, reset reason, time-sync state — on a timer. |
| **A console** | `<Device>/console/in` runs a command and answers on `console/out`. Same grammar over serial. |
| **Request/response** | MQTTP: `<Device>/console/controlled/<id>/in` → `/out`, chunked for long replies. Every built-in command is an API. |
| **Built-in commands** | `PING REBOOT BOOTINFO HARDWAREINFO SYSTEMINFO TIME FS MQTT CONFIG SYSTEMCONFIGS WIFI HTTPSERVER SCHEDULER TIMERS WS` |
| **Timers and a scheduler** | Interval callbacks, and a persisted wall-clock scheduler that fires *commands* — the network's cron. |
| **Persistent config** | A key/value store on LittleFS, editable over the console, with change callbacks. |
| **Time** | Synced from the backend over MQTT; no NTP dependency on the device. |
| **Server variables** | Optimistic remote state: change locally, publish, assert against the device ~10 s later, roll back if it never agreed. |
| **OTA** | ArduinoOTA behind a flag, with the running-OTA state exposed so bus-banging tasks can sit it out. |

The device adds its own components — sensors, actuators, a controller — and a
resolver for its own commands. Everything above is already there.

## The pieces around a device

- **The Dashboard** — a wall panel with no hardware of its own. It listens to
  everything on the broker and drives an AC, a light and an RGB light that live
  on other devices, through the library's *Services*.
- **The backend** (`MwNightmareSystem`) — discovers devices, adopts fresh ones,
  persists every reading into TimescaleDB, and runs a rule engine that turns
  sensor values into commands.
- **The MCP** — this documentation, the source and the examples, exposed to an
  AI assistant so it can answer questions about the library from the actual
  code rather than from memory. See [MCP setup](/docs/mcp).

## Where to go next

- [Getting started](/docs/getting-started) — a device in five files.
- [Device architecture](/docs/architecture) — the long form: vocabulary, rules,
  the survey of every device, and the retrofit plan.
- [Protocols](/docs/protocols/topics) — exactly what goes on the wire.
- [Modules](/docs/modules/mqtt) — one page per library module.
