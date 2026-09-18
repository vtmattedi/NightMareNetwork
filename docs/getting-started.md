---
title: Getting started
description: From an empty PlatformIO project to a device that announces itself, answers PING and publishes telemetry.
section: getting-started
order: 2
---

# Getting started

A NightMare device is a PlatformIO project with the library as a Git
dependency, two headers the project supplies, and a `main.cpp` that wires the
library up in a fixed order. This page gets you to a device that shows up on
the broker, answers commands and publishes telemetry. Adding sensors and a
controller is covered in [Device architecture](/docs/architecture).

## 1. Depend on the library

```ini
[env:esp32c3-supermini]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.39/platform-espressif32.zip
board = nologo_esp32c3_super_mini
framework = arduino
board_build.partitions = min_spiffs.csv
build_flags =
	-I "${platformio.include_dir}"
	-D ESP32_C3
lib_deps =
	https://github.com/vtmattedi/NightMareNetwork.git
	bblanchon/ArduinoJson@^6.21.3
```

Two things in there are easy to get wrong:

- **The platform.** The library's MQTT module uses the nested
  `esp_mqtt_client_config_t` that only exists from ESP-IDF 5, which means
  Arduino core 3.x, which means the pioarduino fork. The registry's
  `espressif32` stops at core 2.x and will not compile.
- **The `-I` flag is required.** PlatformIO puts `include/` on the path when
  compiling *your* sources, but libraries are compiled with their own include
  path. Without the flag the library cannot find the two headers below.

`-D ESP32_C3` is the library's own switch, not an Arduino one: it points the
serial command resolver at the C3's native-USB `HWCDC` console instead of
`HardwareSerial`. Leave it out on a classic ESP32.

## 2. Supply the two headers

The library is configured by two files the **project** owns. Copy the
templates into `include/`:

```sh
cp .pio/libdeps/<env>/NightMareNetwork/src/Core/exemple.creds.h        include/creds.h
cp .pio/libdeps/<env>/NightMareNetwork/src/Modules.example.config.h    include/Modules.config.h
```

| file | holds | tracked? |
| --- | --- | --- |
| `include/creds.h` | WiFi SSID/password, MQTT host/port/user/password, root CA, GMT offset | **no** — add `creds.h` to `.gitignore` |
| `include/Modules.config.h` | the `COMPILE_*` switches that decide which modules are built | yes |

They live in the project rather than in `.pio/libdeps/` because PlatformIO
wipes and re-clones that folder on `pio pkg update`, and the library gitignores
both names — a copy left there is recoverable from nowhere.

## 3. The minimal `main.cpp`

```cpp
#include <Arduino.h>
#include <NightMareNetwork.h>

// Commands the library does not know fall through to this.
NightMareResults localHandleNightMareCommand(const NightMareMessage &message)
{
    NightMareResults res;
    if (message.command == "HELLO")
    {
        res.result = true;
        res.response = "Hello from " + String(getDeviceName());
        return res;
    }
    res.result = false;
    res.response = "Unknown command. Available: [HELLO].";
    return res;
}

void onWifiConnected(bool firstConnection)
{
    if (firstConnection)
        MQTT_Init(REMOTE_MQTT);
}

void setup()
{
    Config.begin();                         // 1. first: everything below may read it
    Serial.begin(115200);
    Serial.println(getDeviceName());
    setCommandResolver(localHandleNightMareCommand);
    WiFi_onConnected(onWifiConnected);      // 2. MQTT starts on the first connection
    WiFi_Auto();
}

void loop()
{
    Timers.run();
    scheduler.run();
    NightMareCommand_SerialResolver(&Serial, '\n');
}
```

The order in `setup()` is not cosmetic: `Config.begin()` comes first because
the device name and every module's settings come from it; the resolver is
registered before WiFi so a command arriving on first connect has somewhere to
go; and MQTT is started from the WiFi-connected callback rather than from
`setup()`, because there is no network to connect to yet.

## 4. First boot

Flash it and watch the broker. Within a few seconds of joining WiFi:

```
Esp32-nm-a1b2c3/status      online            (retained)
Esp32-nm-a1b2c3/console/out Booted
Esp32-nm-a1b2c3/telemetry   {"System":{"Uptime":4,"FreeHeap":18.2,...}}
```

The name is the factory name — `Esp32-nm-` plus the eFuse MAC — until the
device is adopted. Talk to it on its console:

```sh
mosquitto_pub -t 'Esp32-nm-a1b2c3/console/in' -m 'PING'
# → Esp32-nm-a1b2c3/console/out  PONG
mosquitto_pub -t 'Esp32-nm-a1b2c3/console/in' -m 'HELLO'
# → Esp32-nm-a1b2c3/console/out  Hello from Esp32-nm-a1b2c3
```

The same commands work typed into the serial monitor.

## 5. Name it

The device name is runtime config, and the backend's adoption flow sets it the
same way you would by hand:

```
CONFIG SET _device_name "Adler" -p
→ {"_device_name":"Adler", "saved":true}
```

`-p` marks the key privileged (it starts with `_`). Reboot, and the device
comes back as `Adler/...`. Everything that addresses a device — the Dashboard's
bindings, a controller's network sensor, a backend action — uses this name,
case-sensitively.

## What next

- Give it a sensor and publish readings the network understands:
  [Sensors](/docs/protocols/sensors).
- Give it a board revision registry so the pin map survives re-wiring:
  [Device architecture §5.5](/docs/architecture).
- Let an AI assistant read these docs while you work: [MCP setup](/docs/mcp).
