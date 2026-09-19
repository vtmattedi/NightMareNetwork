---
title: Legacy and migration
description: Historical mechanisms and how their behavior maps to the resource architecture.
section: architecture
order: 2
---

# Legacy and migration

`Legacy/src` keeps the older implementation outside the active PlatformIO library build. It is reference material for projects that still speak the earlier protocol. It does not enter `<NightMare.h>`.

| Earlier mechanism | Current path |
| --- | --- |
| Sensor JSON and information declarations | Read-only `NetValue<T>` with optional metadata |
| `ServerVariable` optimistic synchronization | Local authoritative Value plus remote mirror and write request |
| Special UI and controller command topics | Writable Values and Actions registered by a Service |
| `Xtra` command resolver and MQTT preprocessing | `Network/Dispatcher` routes typed operations to `ResourceManager` |
| `console/in` command resolver | Console and CommandRouter answer operator commands and invoke registered Actions |
| `Timers` and persisted command Scheduler | Runtime Scheduler Jobs using monotonic or wall-clock time |
| Direct TimeLib use | `Core/Time` uses the system epoch clock |
| OTA under Core | Pollable `Platform/OtaService` |
| TCP device protocol | Historical predecessor to the MQTT Resource mapping |

The old TCP layer evolved before resources became the network interface. It remains in `Legacy/src/TCP` for context; new participants use the current Resource operations and schema. Persistent Jobs, full namespace isolation, NTP and advanced bridge rules are not implemented by this rewrite.
