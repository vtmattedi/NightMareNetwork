#include <NightMare/Platform/GpioLightService.h>

namespace NightMare {

bool GpioLightService::begin(ResourceManager& manager, bool initialState) {
    if (_manager || !manager.add(_enabled) || !manager.add(_toggle)) return false;
    _manager = &manager;
    pinMode(_pin, OUTPUT);
    manager.onWrite(_enabled, handleWrite, this);
    manager.onAction(_toggle, handleToggle, this);
    set(initialState);
    return true;
}

void GpioLightService::set(bool enabled) {
    if (!_manager) return;
    digitalWrite(_pin, enabled ? HIGH : LOW);
    _manager->set(_enabled, enabled);
}

ActionStatus GpioLightService::handleWrite(void* context, NetResource&, const String& requested) {
    bool enabled;
    if (!NetCodec<bool>::decode(requested, enabled)) return ActionStatus::INVALID_ARGUMENT;
    static_cast<GpioLightService*>(context)->set(enabled);
    return ActionStatus::OK;
}

ActionStatus GpioLightService::handleToggle(void* context, NetResource&, const String& arguments, String&) {
    if (arguments.length()) return ActionStatus::INVALID_ARGUMENT;
    auto* light = static_cast<GpioLightService*>(context);
    light->set(!light->enabled());
    return ActionStatus::OK;
}

} // namespace NightMare
