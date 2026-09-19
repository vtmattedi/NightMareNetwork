#include <NightMare/Runtime/Runtime.h>

namespace NightMare {

bool Runtime::add(Poller poller, void* context) {
    if (!poller) return false;
    for (auto& component : _components)
        if (component.poller == poller && component.context == context) return false;
    for (auto& component : _components)
        if (!component.poller) { component = {poller, context}; return true; }
    return false;
}
void Runtime::tick() {
    _scheduler.tick();
    for (const auto& component : _components) if (component.poller) component.poller(component.context);
}
bool Runtime::startManaged(uint32_t stackBytes, UBaseType_t priority, uint32_t periodMs) {
    if (_task || !periodMs) return false;
    _periodMs = periodMs;
    return xTaskCreate(taskMain, "nm_runtime", stackBytes, this, priority, &_task) == pdPASS;
}
void Runtime::stopManaged() {
    if (_task) { vTaskDelete(_task); _task = nullptr; }
}
void Runtime::taskMain(void* argument) {
    auto* runtime = static_cast<Runtime*>(argument);
    for (;;) {
        runtime->tick();
        vTaskDelay(pdMS_TO_TICKS(runtime->_periodMs));
    }
}

} // namespace NightMare
