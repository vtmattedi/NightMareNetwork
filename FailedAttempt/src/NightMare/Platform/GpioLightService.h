#pragma once
#include <NightMare/Resources/ResourceManager.h>

namespace NightMare {

// A local service. Remote clients see only its Value and Action resources.
class GpioLightService {
public:
    GpioLightService(uint8_t outputPin, const char* valueId = "lightEnabled",
                    const char* toggleId = "toggleLight")
        : _pin(outputPin), _enabled(valueId, NetAccess::READ_WRITE),
          _toggle(toggleId, ActionResponse::ACK) {}
    bool begin(ResourceManager& manager, bool initialState = false);
    void set(bool enabled);
    bool enabled() const { return _enabled.get(); }
    NetValue<bool>& value() { return _enabled; }
    NetAction<void>& action() { return _toggle; }
private:
    static ActionStatus handleWrite(void* context, NetResource&, const String& requested);
    static ActionStatus handleToggle(void* context, NetResource&, const String& arguments, String&);
    uint8_t _pin;
    ResourceManager* _manager = nullptr;
    NetValue<bool> _enabled;
    NetAction<void> _toggle;
};

} // namespace NightMare
