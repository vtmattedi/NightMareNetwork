#include <NightMare/Features.h>
#if NIGHTMARE_ENABLE_TELEMETRY
#include <NightMare/Services/TelemetryService.h>
#include <NightMare/Platform/Esp32SystemInfo.h>
#include <NightMare/Core/Time.h>
#if NIGHTMARE_ENABLE_WIFI
#include <WiFi.h>
#endif

namespace NightMare {

bool TelemetryService::begin(ResourceManager& resources, Runtime& runtime,
                             const char* firmwareVersion) {
    constexpr size_t slots =
#if NIGHTMARE_ENABLE_WIFI
        11;
#else
        8;
#endif
    if (_resources || resources.registry().count() + slots > NIGHTMARE_MAX_RESOURCES) return false;
    if (!resources.add(_uptime, {true, 30000}) ||
        !resources.add(_freeHeap, {true, 30000}) ||
        !resources.add(_minFreeHeap, {true, 30000}) ||
        !resources.add(_chipModel) || !resources.add(_flashSize) ||
        !resources.add(_bootCount) || !resources.add(_firmwareVersion) ||
        !resources.add(_timeSynced, {true, 30000})
#if NIGHTMARE_ENABLE_WIFI
        || !resources.add(_wifiConnected, {true, 30000}) ||
        !resources.add(_wifiRssi, {true, 30000}) ||
        !resources.add(_ipAddress, {true, 30000})
#endif
        ) return false;
    auto hardware = Esp32SystemInfo::hardware();
    auto boot = Esp32SystemInfo::boot(firmwareVersion);
    resources.set(_chipModel, hardware.chipModel);
    resources.set(_flashSize, hardware.flashBytes);
    resources.set(_bootCount, boot.bootCount);
    resources.set(_firmwareVersion, boot.firmwareVersion);
    _resources = &resources;
    update();
    return runtime.scheduler().every("nm_telemetry", 1000, poll, this);
}

void TelemetryService::update() {
    if (!_resources) return;
    const auto status = Esp32SystemInfo::runtime();
    _resources->set(_uptime, status.uptimeMs / 1000);
    _resources->set(_freeHeap, status.freeHeapBytes);
    _resources->set(_minFreeHeap, status.minimumFreeHeapBytes);
    _resources->set(_timeSynced, Time::valid());
#if NIGHTMARE_ENABLE_WIFI
    bool connected = WiFi.status() == WL_CONNECTED;
    _resources->set(_wifiConnected, connected);
    if (connected) {
        _resources->set(_wifiRssi, static_cast<int32_t>(WiFi.RSSI()));
        _resources->set(_ipAddress, WiFi.localIP().toString());
    }
#endif
}

} // namespace NightMare
#endif
