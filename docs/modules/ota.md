---
title: OTA
description: ArduinoOTA behind a flag, the events it raises, the flags it sets, and why tasks that bit-bang a bus watch for it.
section: modules
order: 80
---

# OTA — `Core/OTA.h`

Over-the-air firmware upload with ArduinoOTA (the `espota` protocol
PlatformIO speaks). Compiled with `COMPILE_OTA`; started by the WiFi module on
first connect.

## API

```cpp
enum OTA_INFO { OTA_START, OTA_END, OTA_ERROR, OTA_PROGRESS };
typedef void (*ota_callback_t)(OTA_INFO info, int data);   // data: percent for PROGRESS, error code for ERROR

void initOTA();
void onOTAEvent(ota_callback_t callback);
```

`OTA_TIMEOUT_MS` (5000) bounds how long the handler waits between packets;
the handler runs on its own task at `OTA_TASK_PRIORITY` (1).

## Flags

| `SystemSettings` key | meaning |
| --- | --- |
| `ota_enabled` | OTA is compiled in and listening; reported in telemetry |
| `ota_running` | an upload is in progress |

`ota_running` exists for other tasks. A 1-Wire bit slot, a WS2812 frame, a
mains-period ADC busy-wait — each runs with interrupts masked, and an upload
stalling behind them fails. Mycroft-headless's sensor tasks check the flag
each pass and sleep 5 s instead of touching the bus:

```cpp
if (!SystemSettings.getFlag("ota_running")) readSensor();
```

## Uploading

```ini
upload_protocol = espota
upload_port = 10.10.3.11        ; the device's IP, or its mDNS name
```

Two app partitions are required (`min_spiffs.csv` has them; `default.csv`
does too but caps the app at 1.25 MB). A firmware too large for two slots —
the Dashboard's, at ~2 MB with LVGL and weather icons — cannot use OTA at all,
which is why that project turns `COMPILE_OTA` off: left on, it still holds a
task, an mDNS responder and a UDP listener, and that heap is what its TLS
handshake runs short of.

## Console

`REBOOT` after a successful upload is automatic. There is no OTA console
command; the state is visible in `SYSTEMINFO` as `OTA_enabled`.
