#include <NightMare/Features.h>
#if NM_ENABLE_WIFI_RADIO

#include "NmWifiRadio.h"

#include <Core/Logs.h>
#include <esp_event.h>
#include <esp_wifi.h>

namespace
{
constexpr char Tag[] = "WiFiRadio";

bool initialized = false; // esp_wifi_init done (and the station netif, if any)
bool running = false;     // esp_wifi_start done
esp_netif_t *stationNetif = nullptr;
WiFiRadioStateCallback stateCallback = nullptr;

bool initialize()
{
    if (initialized)
        return true;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        LOG_ERROR(Tag, "esp_netif_init failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        LOG_ERROR(Tag, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return false;
    }
#if NM_ENABLE_WIFI
    // Before esp_wifi_init/start, not when the station first needs it -- see
    // WiFiRadio_stationNetif().
    if (stationNetif == nullptr)
        stationNetif = esp_netif_create_default_wifi_sta();
    if (stationNetif == nullptr)
    {
        LOG_ERROR(Tag, "Could not create the station netif");
        return false;
    }
#endif
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    // No driver NVS at all. The station's credentials live in NightMare's own
    // settings, and esp_wifi_init would otherwise load the driver's last saved
    // STA config -- WIFI_STORAGE_RAM below comes too late to stop that. On a
    // radio-only device that stale SSID tells ESP-NOW a station owns the
    // channel (EspNowClient::freeToTune), so it never hops, while nothing is
    // actually joining an AP.
    config.nvs_enable = 0;
    err = esp_wifi_init(&config);
    if (err != ESP_OK)
    {
        LOG_ERROR(Tag, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return false;
    }
    if (esp_wifi_set_storage(WIFI_STORAGE_RAM) != ESP_OK ||
        esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK)
    {
        LOG_ERROR(Tag, "Could not put the driver in STA mode");
        esp_wifi_deinit();
        return false;
    }
    initialized = true;
    return true;
}
}

void WiFiRadio_onState(WiFiRadioStateCallback callback) { stateCallback = callback; }

bool WiFiRadio_start()
{
    if (running)
        return true;
    if (!initialize())
        return false;
    const esp_err_t err = esp_wifi_start();
    if (err != ESP_OK)
    {
        LOG_ERROR(Tag, "esp_wifi_start failed: %s", esp_err_to_name(err));
        return false;
    }
    running = true;
    uint8_t mac[6] = {};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    LOG(Tag, "Radio up on ch %u, %02X:%02X:%02X:%02X:%02X:%02X", WiFiRadio_channel(), mac[0],
        mac[1], mac[2], mac[3], mac[4], mac[5]);
    if (stateCallback != nullptr)
        stateCallback(true);
    return true;
}

void WiFiRadio_stop()
{
    if (!initialized)
        return;
    if (running)
    {
        // Report first, while the driver still works: whatever runs on top of
        // the radio gets to shut down cleanly (ESP-NOW deinitialises itself).
        if (stateCallback != nullptr)
            stateCallback(false);
        esp_wifi_stop();
        running = false;
    }
    esp_wifi_deinit();
#if NM_ENABLE_WIFI
    esp_netif_destroy_default_wifi(stationNetif);
    stationNetif = nullptr;
#endif
    initialized = false;
    LOG(Tag, "Radio down");
}

bool WiFiRadio_running() { return running; }

esp_netif_t *WiFiRadio_stationNetif() { return stationNetif; }

uint8_t WiFiRadio_channel()
{
    if (!running)
        return 0;
    uint8_t primary = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&primary, &second);
    return primary;
}

#endif // NM_ENABLE_WIFI_RADIO
