#pragma once
#include <Arduino.h>

namespace NightMare {

// Pollable OTA adapter. Register tick() with Runtime when the application
// chooses to offer Arduino OTA; it creates no task of its own.
class OtaService {
public:
    bool begin(const char* hostName, const char* password = nullptr);
    void tick();
private:
    bool _started = false;
};

} // namespace NightMare
