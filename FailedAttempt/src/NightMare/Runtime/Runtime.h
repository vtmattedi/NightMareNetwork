#pragma once
#include <NightMare/Runtime/Scheduler.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

namespace NightMare {

class Runtime {
public:
    using Poller = void (*)(void* context);
    Scheduler& scheduler() { return _scheduler; }
    bool add(Poller poller, void* context = nullptr);
    void tick();
    bool startManaged(uint32_t stackBytes = 4096, UBaseType_t priority = 1,
                      uint32_t periodMs = 20);
    void stopManaged();
private:
    struct Component { Poller poller = nullptr; void* context = nullptr; };
    static void taskMain(void* argument);
    Component _components[8];
    Scheduler _scheduler;
    TaskHandle_t _task = nullptr;
    uint32_t _periodMs = 20;
};

} // namespace NightMare
