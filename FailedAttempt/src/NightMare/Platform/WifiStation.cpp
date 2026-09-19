#include <NightMare/Features.h>
#if NIGHTMARE_ENABLE_WIFI
#include <NightMare/Platform/WifiStation.h>

namespace NightMare {

bool WifiStation::begin(const char* ssid, const char* password, const char* hostName) {
    if (!ssid || !*ssid || !hostName || !*hostName) return false;
    _ssid = ssid;
    _password = password;
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(hostName);
    WiFi.begin(_ssid, _password);
    _lastAttemptMs = millis();
    return true;
}

bool WifiStation::tick(uint32_t nowMs) {
    bool online = connected();
    bool newlyConnected = online && !_wasConnected;
    _wasConnected = online;
    if (!online && _ssid && static_cast<uint32_t>(nowMs - _lastAttemptMs) >= 10000) {
        WiFi.reconnect();
        _lastAttemptMs = nowMs;
    }
    return newlyConnected;
}

} // namespace NightMare
#endif
