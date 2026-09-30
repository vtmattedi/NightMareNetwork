#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_WIFI_RADIO

// ESP-IDF only: owns the Wi-Fi driver itself -- netif/event loop, esp_wifi_init,
// STA mode, esp_wifi_start -- and nothing above it. Joining an AP and getting an
// IP belong to the station layer (Network/WiFiIP); ESP-NOW needs only this.
//
//   radio running   ESP-NOW can work, on whatever channel the radio is on
//   IP link up      the station joined an AP and has an address (MQTT, SNTP, OTA)
//
// Those used to be one "Wi-Fi started" state. They are not: a radio-only device
// never joins an AP, and a station that loses its AP leaves the radio running.
#include <esp_netif.h>
#include <cstdint>

// Called from the task that started or stopped the radio -- never the ESP
// event task -- so it may do real work.
using WiFiRadioStateCallback = void (*)(bool running);

void WiFiRadio_onState(WiFiRadioStateCallback callback);

// Brings the driver up in STA mode and starts it, without configuring or
// joining any AP. Idempotent while running.
bool WiFiRadio_start();
// Stops and deinitialises the driver. Anything on top of it (the station,
// ESP-NOW) stops working until WiFiRadio_start() is called again.
void WiFiRadio_stop();
bool WiFiRadio_running();

// The default station netif, created alongside the driver when the IP station
// is compiled in (NM_ENABLE_WIFI), nullptr otherwise. It has to exist before
// esp_wifi_start: it follows the driver's STA events, and one created after the
// radio started misses STA_START and never brings its interface up.
esp_netif_t *WiFiRadio_stationNetif();

// Primary channel the radio is on right now, 0 while not running.
uint8_t WiFiRadio_channel();

#endif // NM_ENABLE_WIFI_RADIO
