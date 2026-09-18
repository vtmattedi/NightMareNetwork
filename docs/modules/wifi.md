---
title: WiFi
description: Bringing the radio up, the connected callback every device hangs MQTT off, and changing networks from the console.
section: modules
order: 70
---

# WiFi — `Core/bWIFI.h`

Station-mode WiFi with a connected callback and console control. Compiled
with `COMPILE_WIFI_MODULE`; needs `DEFAULT_SSID` and `DEFAULT_PASSWORD` from
`creds.h`, and the build fails with a clear `#error` if either is missing.

## API

```cpp
typedef void (*WiFiConnectedCallback)(bool firstConnection);

void        WiFi_onConnected(WiFiConnectedCallback cb);
void        WiFi_Auto();                                            // connect with what the device has
bool        WiFi_Connect(const char *ssid, const char *password, int timeoutMs = 0,
                         void *waitCallback(unsigned int) = nullptr);   // blocking
bool        WiFi_ConnectAsync(const char *ssid, const char *password, bool deleteAfterConnect = true);
void        WiFi_Disconnect();
bool        WiFi_ChangeCredentials(const String &ssid, const String &password);
const char *WiFi_getAuthTypeName(wifi_auth_mode_t authType);
```

`WiFi_Auto()` is what `setup()` calls: it connects with the credentials the
device has — the ones saved by `WiFi_ChangeCredentials()` / `WIFI CHANGE` if
that ever happened, otherwise the defaults from `creds.h` — and reconnects on
its own after a drop.

## The connected callback

```cpp
void onWifiConnected(bool firstConnection)
{
    if (firstConnection)
        MQTT_Init(REMOTE_MQTT);
}
```

`firstConnection` is true once per boot. This is where MQTT starts, because
there is nothing to connect to before it. Two things about the callback:

- **It runs on the WiFi event task**, not on `loop()`. Do the minimum there.
  The Dashboard only sets a flag in it and starts MQTT from `loop()` behind a
  heap check, because `MQTT_Init` with TLS needs ~60 KB contiguous and a
  failed init on a small heap boot-loops.
- With `COMPILE_OTA`, `initOTA()` is hooked in here by the library.

## Console

| command | what |
| --- | --- |
| `WIFI IP` | current address |
| `WIFI STATE` | connection state, SSID, RSSI |
| `WIFI RECONNECT` | drop and reconnect |
| `WIFI SCAN` | networks in range with auth type and RSSI |
| `WIFI CHANGE <ssid> <password>` | save new credentials and switch |

The web backend's device page uses `SCAN` and `CHANGE` over MQTTP to move a
device between networks without a reflash — which is also the way to strand
one, so `CHANGE` is worth doing while standing next to the device.
