#include <NightMare/Platform/Esp32SystemInfo.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>

namespace NightMare {

RTC_DATA_ATTR static uint32_t nmBootCount = 0;

HardwareInfo Esp32SystemInfo::hardware() {
    HardwareInfo info;
    info.chipModel = ESP.getChipModel();
    info.chipRevision = ESP.getChipRevision();
    info.hardwareId = String(ESP.getEfuseMac(), HEX);
    info.cpuMHz = ESP.getCpuFreqMHz();
    info.flashBytes = ESP.getFlashChipSize();
    info.psramBytes = ESP.getPsramSize();
    info.sdkVersion = ESP.getSdkVersion();
    return info;
}

BootInfo Esp32SystemInfo::boot(const char* firmwareVersion) {
    static bool counted = false;
    if (!counted) { ++nmBootCount; counted = true; }
    BootInfo info;
    info.bootCount = nmBootCount;
    info.resetReason = static_cast<int>(esp_reset_reason());
    info.wakeReason = static_cast<int>(esp_sleep_get_wakeup_cause());
    info.firmwareVersion = firmwareVersion ? firmwareVersion : "";
    return info;
}

RuntimeInfo Esp32SystemInfo::runtime() {
    RuntimeInfo info;
    info.uptimeMs = millis();
    info.freeHeapBytes = ESP.getFreeHeap();
    info.minimumFreeHeapBytes = ESP.getMinFreeHeap();
    info.largestFreeBlockBytes = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    return info;
}

String Esp32SystemInfo::hardwareText() {
    const auto info = hardware();
    return "chip=" + info.chipModel + " rev=" + String(info.chipRevision) +
        " hardwareId=" + info.hardwareId + " cpuMHz=" + String(info.cpuMHz) +
        " flashBytes=" + String(info.flashBytes) + " psramBytes=" + String(info.psramBytes) +
        " sdk=" + info.sdkVersion;
}

String Esp32SystemInfo::bootText(const char* firmwareVersion) {
    const auto info = boot(firmwareVersion);
    return "bootCount=" + String(info.bootCount) + " resetReason=" + String(info.resetReason) +
        " wakeReason=" + String(info.wakeReason) + " firmware=" + info.firmwareVersion;
}

String Esp32SystemInfo::statusText() {
    const auto info = runtime();
    return "uptimeMs=" + String(info.uptimeMs) + " freeHeap=" + String(info.freeHeapBytes) +
        " minFreeHeap=" + String(info.minimumFreeHeapBytes) +
        " largestBlock=" + String(info.largestFreeBlockBytes);
}

} // namespace NightMare
