#pragma once
#include <NightMare/Features.h>
#include <NightMare/Resources/ResourceManager.h>
#include <NightMare/Runtime/Runtime.h>

namespace NightMare {

// Ordinary locally owned Values. Runtime drives updates; ResourceManager owns
// publication, replay and schemas.
class TelemetryService {
public:
    bool begin(ResourceManager& resources, Runtime& runtime,
               const char* firmwareVersion = "");
    void update();
private:
    static void poll(void* context) { static_cast<TelemetryService*>(context)->update(); }
    ResourceManager* _resources = nullptr;
    NetValue<uint32_t> _uptime{"uptimeSeconds"};
    NetValue<uint32_t> _freeHeap{"freeHeapBytes"};
    NetValue<uint32_t> _minFreeHeap{"minimumFreeHeapBytes"};
    NetValue<String> _chipModel{"chipModel"};
    NetValue<uint32_t> _flashSize{"flashBytes"};
    NetValue<uint32_t> _bootCount{"bootCount"};
    NetValue<String> _firmwareVersion{"firmwareVersion"};
    NetValue<bool> _timeSynced{"timeSynced"};
#if NIGHTMARE_ENABLE_WIFI
    NetValue<bool> _wifiConnected{"wifiConnected"};
    NetValue<int32_t> _wifiRssi{"wifiRssi"};
    NetValue<String> _ipAddress{"ipAddress"};
#endif
};

} // namespace NightMare
