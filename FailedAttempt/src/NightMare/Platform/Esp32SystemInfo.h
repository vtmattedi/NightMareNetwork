#pragma once
#include <Arduino.h>

namespace NightMare {

// Platform information is available without Network or Resource registration.
struct BootInfo {
    uint32_t bootCount = 0;
    int resetReason = 0;
    int wakeReason = 0;
    String firmwareVersion;
};

struct HardwareInfo {
    String chipModel;
    uint8_t chipRevision = 0;
    String hardwareId;
    uint32_t cpuMHz = 0;
    uint32_t flashBytes = 0;
    uint32_t psramBytes = 0;
    String sdkVersion;
};

struct RuntimeInfo {
    uint32_t uptimeMs = 0;
    uint32_t freeHeapBytes = 0;
    uint32_t minimumFreeHeapBytes = 0;
    uint32_t largestFreeBlockBytes = 0;
};

class Esp32SystemInfo {
public:
    static HardwareInfo hardware();
    static BootInfo boot(const char* firmwareVersion = "");
    static RuntimeInfo runtime();
    static String hardwareText();
    static String bootText(const char* firmwareVersion = "");
    static String statusText();
};

} // namespace NightMare
