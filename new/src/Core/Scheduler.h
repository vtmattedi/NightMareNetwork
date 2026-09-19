#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define SCHEDULER_DEFAULT_STACK_SIZE 8192
#define SCHEDULER_DEFAULT_PRIORITY 1 // same as loopTaskPriority

// Wall deadlines are Unix seconds. Monotonic deadlines and intervals are milliseconds.
enum class SchedulerClock : uint8_t
{
    Wall,
    Monotonic
};

struct Job
{
    bool active = false;
    uint32_t id = 0;
    String label;
    String command;
    SchedulerClock clock = SchedulerClock::Monotonic;
    uint32_t due = 0;
    uint32_t interval = 0; // Zero means run once.
};

class Scheduler
{
public:
    static constexpr uint8_t MaxJobs = 30;

    // Central command forms: JOB AT, JOB AFTER, JOB EVERY, JOB LIST, JOB DELETE, JOB CLEAR.
    // Wall jobs use epoch seconds and persist. Monotonic jobs use milliseconds and reset on boot.
    // Loads persisted wall jobs and starts the scheduler task.
    // Monotonic jobs remain usable if storage is unavailable.
    bool begin();
    int32_t atWall(const String &label, const String &command, uint32_t epochSeconds);
    int32_t after(const String &label, const String &command, uint32_t delayMs);
    int32_t everyWall(const String &label, const String &command, uint32_t intervalSeconds);
    int32_t everyMonotonic(const String &label, const String &command, uint32_t intervalMs);
    bool remove(const String &label);
    bool remove(uint32_t id);
    bool clear();
    String list();
    // Called by the scheduler task. May also be called directly when needed.
    void tick();

private:
    Job jobs_[MaxJobs];
    uint32_t nextId_ = 1;
    bool begun_ = false;
    bool storageReady_ = false;
    bool dispatching_ = false;
    uint32_t nextStorageRetry_ = 0;
    TaskHandle_t schedulerTask_ = nullptr;

    int32_t add(const String &label, const String &command, SchedulerClock clock,
                uint32_t due, uint32_t interval);
    bool save();
    bool load();
    bool importLegacy();
    bool startScheduler(uint8_t priority = SCHEDULER_DEFAULT_PRIORITY, uint32_t stackSize = SCHEDULER_DEFAULT_STACK_SIZE); // Launch scheduler as task
    static void task(void *context);
};

extern Scheduler gScheduler;
