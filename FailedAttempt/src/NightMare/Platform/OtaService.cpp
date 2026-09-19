#include <NightMare/Features.h>
#if NIGHTMARE_ENABLE_OTA
#include <NightMare/Platform/OtaService.h>
#include <ArduinoOTA.h>

namespace NightMare {

bool OtaService::begin(const char* hostName, const char* password) {
    if (_started || !hostName || !*hostName) return false;
    ArduinoOTA.setHostname(hostName);
    if (password && *password) ArduinoOTA.setPassword(password);
    ArduinoOTA.begin();
    _started = true;
    return true;
}

void OtaService::tick() { if (_started) ArduinoOTA.handle(); }

} // namespace NightMare
#endif
