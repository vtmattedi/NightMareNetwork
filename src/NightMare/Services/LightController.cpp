#include <NightMare/Services/LightController.h>

namespace NightMare {

bool LightController::begin(ResourceManager& manager, bool initialState) {
    if (_manager || !manager.add(_enabled) || !manager.add(_toggle)) return false;
    _manager = &manager;
    pinMode(_pin, OUTPUT);
    manager.onWrite(_enabled, handleWrite, this);
    manager.onAction(_toggle, handleToggle, this);
    set(initialState);
    return true;
}

void LightController::set(bool enabled) {
    if (!_manager) return;
    digitalWrite(_pin, enabled ? HIGH : LOW);
    _manager->set(_enabled, enabled);
}

ActionStatus LightController::handleWrite(void* context, NetResource&, const String& requested) {
    bool enabled;
    if (!NetCodec<bool>::decode(requested, enabled)) return ActionStatus::INVALID_ARGUMENT;
    static_cast<LightController*>(context)->set(enabled);
    return ActionStatus::OK;
}

ActionStatus LightController::handleToggle(void* context, NetResource&, const String& arguments, String&) {
    if (arguments.length()) return ActionStatus::INVALID_ARGUMENT;
    auto* light = static_cast<LightController*>(context);
    light->set(!light->enabled());
    return ActionStatus::OK;
}

} // namespace NightMare
