#pragma once
#include <WiFi.h>

namespace NightMare {

class WifiStation {
public:
    bool begin(const char* ssid, const char* password, const char* hostName);
    // Returns true once on each transition to connected.
    bool tick(uint32_t nowMs = millis());
    bool connected() const { return WiFi.status() == WL_CONNECTED; }
private:
    const char* _ssid = nullptr;
    const char* _password = nullptr;
    uint32_t _lastAttemptMs = 0;
    bool _wasConnected = false;
};

} // namespace NightMare
