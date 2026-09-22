#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_SCHEDULER
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

// Who drives tick(). The timing engine and the jobs are the same either way.
enum class SchedulerRunMode : uint8_t
{
    TASK,  // The Scheduler owns a FreeRTOS task that ticks it.
    MANUAL // Nothing ticks it but the application (see tickNightMareESP()).
};

// A plain function or a non-capturing lambda. Captures are not supported.
using SchedulerCallback = void (*)();

// Who a job belongs to. The two never see or block each other: labels are
// unique per scope, and the JOB commands only ever reach USER jobs, so nothing
// typed at a console can list, delete or clear what the framework relies on.
enum class SchedulerJobScope : uint8_t
{
    MANAGED, // Created by framework or application C++ code.
    USER     // Created through JOB commands (console, MQTT, MQTTP).
};

// A job runs exactly one target: a command (text, handed to the command
// handler) or a callback (a function pointer). Never both, never neither.
struct Job
{
    bool active = false;
    uint32_t id = 0;
    SchedulerJobScope scope = SchedulerJobScope::MANAGED;
    String label;
    String command;
    SchedulerClock clock = SchedulerClock::Monotonic;
    uint32_t due = 0;
    uint32_t interval = 0; // Zero means run once.
    SchedulerCallback callback = nullptr;
};

// One timing engine for commands and callbacks. Only wall-clock command jobs
// survive a reboot: a function pointer is runtime state, and a monotonic
// deadline means nothing after a restart.
class Scheduler
{
public:
    static constexpr uint8_t MaxJobs = 30;

    // Central command forms: JOB AT, JOB AFTER, JOB EVERY, JOB LIST, JOB DELETE, JOB CLEAR.

    /// @brief Starts running jobs in the given mode. The first successful call
    /// fixes the mode; a later call asking for the other one fails rather than
    /// quietly moving execution somewhere else. True means the scheduler runs,
    /// whether or not persistent storage is available yet (it is retried).
    /// Jobs may be added before this, but none runs until it has been called
    /// (or, in MANUAL mode, until tick() is serviced). tick() may also be called directly for cooperative/manual execution.
    bool begin(SchedulerRunMode mode = SchedulerRunMode::TASK);
    SchedulerRunMode runMode() const { return mode_; }

    // A command job is MANAGED unless a JOB command creates it as USER. A
    // callback job is always MANAGED: nothing outside C++ can make one.
    int32_t atWall(const String &label, const String &command, uint32_t epochSeconds,
                   SchedulerJobScope scope = SchedulerJobScope::MANAGED);
    int32_t atWall(const String &label, SchedulerCallback callback, uint32_t epochSeconds);

    int32_t after(const String &label, const String &command, uint32_t delayMs,
                  SchedulerJobScope scope = SchedulerJobScope::MANAGED);
    int32_t after(const String &label, SchedulerCallback callback, uint32_t delayMs);

    int32_t everyWall(const String &label, const String &command, uint32_t intervalSeconds,
                      SchedulerJobScope scope = SchedulerJobScope::MANAGED);
    int32_t everyWall(const String &label, SchedulerCallback callback, uint32_t intervalSeconds);

    int32_t everyMonotonic(const String &label, const String &command, uint32_t intervalMs,
                           SchedulerJobScope scope = SchedulerJobScope::MANAGED);
    int32_t everyMonotonic(const String &label, SchedulerCallback callback, uint32_t intervalMs);

    // Conveniences over the forms above: a recurring monotonic callback whose
    // first run is one interval from now, and a one-shot with a generated label.
    int32_t timer(const String &label, SchedulerCallback callback, uint32_t intervalMs);
    int32_t setTimeout(SchedulerCallback callback, uint32_t delayMs);

    // Scoped because a label alone no longer names one job. The MANAGED default
    // lets framework code remove its own jobs without ever touching a USER job
    // that happens to share the label.
    bool remove(const String &label, SchedulerJobScope scope = SchedulerJobScope::MANAGED);
    bool remove(uint32_t id, SchedulerJobScope scope = SchedulerJobScope::MANAGED);
    // Removes every job of one scope; JOB CLEAR uses USER.
    bool clear(SchedulerJobScope scope);
    String list(SchedulerJobScope scope);
    // Called by the scheduler task in TASK mode; by the application in MANUAL mode.
    void tick();

private:
    Job jobs_[MaxJobs];
    uint32_t nextId_ = 1;
    uint32_t nextTimeoutId_ = 0;
    bool initialized_ = false;
    bool modeFixed_ = false;
    SchedulerRunMode mode_ = SchedulerRunMode::TASK;
    bool storageReady_ = false;
    bool dispatching_ = false;
    uint32_t nextStorageRetry_ = 0;
    TaskHandle_t schedulerTask_ = nullptr;

    // Storage and persisted jobs. Never chooses a run mode.
    void ensureInitialized();
    // Each clock/repeat shape computes its deadline here, for either target.
    int32_t scheduleAtWall(SchedulerJobScope scope, const String &label, const String &command,
                           SchedulerCallback callback, uint32_t epochSeconds);
    int32_t scheduleAfter(SchedulerJobScope scope, const String &label, const String &command,
                          SchedulerCallback callback, uint32_t delayMs);
    int32_t scheduleEveryWall(SchedulerJobScope scope, const String &label, const String &command,
                              SchedulerCallback callback, uint32_t intervalSeconds);
    int32_t scheduleEveryMonotonic(SchedulerJobScope scope, const String &label,
                                   const String &command, SchedulerCallback callback,
                                   uint32_t intervalMs);
    // The single place a job is created and validated.
    int32_t add(SchedulerJobScope scope, const String &label, const String &command,
                SchedulerClock clock, uint32_t due, uint32_t interval, SchedulerCallback callback);
    // Removes the matching job, saving if it was persisted. Rolls back on failure.
    bool removeJob(Job &job);
    static bool persists(const Job &job);
    bool save();
    // Writes the persisted jobs, leaving out one scope when omit is given.
    bool writeJobs(const SchedulerJobScope *omit);
    bool load();
    bool startTask(uint8_t priority = SCHEDULER_DEFAULT_PRIORITY, uint32_t stackSize = SCHEDULER_DEFAULT_STACK_SIZE);
    static void task(void *context);
};

extern Scheduler gScheduler;
#endif // NM_ENABLE_SCHEDULER
