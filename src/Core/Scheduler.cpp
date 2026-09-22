#include <NightMare/Features.h>
#if NM_ENABLE_SCHEDULER
#include "Scheduler.h"

#include "NightMareCommand.h"
#include "StateStore.h"
#include "Time.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <freertos/semphr.h>

namespace
{
    constexpr const char *JobsFile = "/jobs.json";
    constexpr uint32_t MaxInterval = 0x7fffffffUL;
    constexpr uint32_t StorageRetryMs = 5000;
    constexpr uint32_t SchedulerPollMs = 100;
    constexpr size_t MaxLabelLength = 64;

    SemaphoreHandle_t jobMutex()
    {
        static SemaphoreHandle_t mutex = xSemaphoreCreateRecursiveMutex();
        return mutex;
    }

    class JobGuard
    {
    public:
        JobGuard() : mutex_(jobMutex()) { lock(); }
        ~JobGuard() { unlock(); }

        void lock()
        {
            if (mutex_ != nullptr && !locked_)
                locked_ = xSemaphoreTakeRecursive(mutex_, portMAX_DELAY) == pdTRUE;
        }

        void unlock()
        {
            if (locked_)
            {
                xSemaphoreGiveRecursive(mutex_);
                locked_ = false;
            }
        }

    private:
        SemaphoreHandle_t mutex_;
        bool locked_ = false;
    };

    bool wallTimeReady()
    {
        return NightMare::Time::valid();
    }
}

Scheduler gScheduler;

void Scheduler::ensureInitialized()
{
    if (initialized_)
    {
        if (!storageReady_ && static_cast<int32_t>(millis() - nextStorageRetry_) >= 0)
        {
            storageReady_ = PersistentSettings.begin();
            nextStorageRetry_ = millis() + StorageRetryMs;
            if (storageReady_)
                load();
        }
        return;
    }
    initialized_ = true;
    storageReady_ = PersistentSettings.begin();
    nextStorageRetry_ = millis() + StorageRetryMs;
    if (storageReady_)
        load();
}

bool Scheduler::begin(SchedulerRunMode mode)
{
    JobGuard guard;
    ensureInitialized();
    if (modeFixed_)
    {
        if (mode != mode_)
        {
            LOG_ERROR("Scheduler", "Already running in %s mode; refusing to switch",
                      mode_ == SchedulerRunMode::TASK ? "TASK" : "MANUAL");
            return false;
        }
        return mode_ == SchedulerRunMode::MANUAL || startTask();
    }
    // Only a mode that actually started is fixed, so a failed task creation
    // can be retried or replaced by MANUAL.
    if (mode == SchedulerRunMode::TASK && !startTask())
        return false;
    mode_ = mode;
    modeFixed_ = true;
    return true;
}

bool Scheduler::startTask(uint8_t priority, uint32_t stackSize)
{
    if (schedulerTask_ != nullptr)
        return true;
    if (jobMutex() == nullptr || stackSize == 0 || priority >= configMAX_PRIORITIES)
        return false;

    TaskHandle_t handle = nullptr;
    if (xTaskCreate(task, "JobScheduler", stackSize, this, priority, &handle) != pdPASS)
        return false;
    schedulerTask_ = handle;
    return true;
}

void Scheduler::task(void *context)
{
    Scheduler *manager = static_cast<Scheduler *>(context);
    const TickType_t interval = pdMS_TO_TICKS(SchedulerPollMs) > 0
                                    ? pdMS_TO_TICKS(SchedulerPollMs)
                                    : 1;
    for (;;)
    {
        manager->tick();
        vTaskDelay(interval);
    }
}

bool Scheduler::persists(const Job &job)
{
    // Only what can be executed from text after a reboot.
    return job.active && job.clock == SchedulerClock::Wall && job.callback == nullptr;
}

int32_t Scheduler::add(const String &label, const String &command, SchedulerClock clock,
                       uint32_t due, uint32_t interval, SchedulerCallback callback)
{
    JobGuard guard;
    ensureInitialized();
    const bool hasCommand = command.length() != 0;
    const bool hasCallback = callback != nullptr;
    if (hasCommand == hasCallback)
        return -1; // Exactly one execution target.
    if (label.length() == 0 || label.length() > MaxLabelLength ||
        command.length() > NM_MAX_MESSAGE_LEN)
        return -1;
    const bool persisted = clock == SchedulerClock::Wall && hasCommand;
    if (persisted && !storageReady_)
        return -1;
    if (nextId_ == 0 || nextId_ > INT32_MAX)
        return -1;

    for (const Job &job : jobs_)
        if (job.active && job.label == label)
            return -1;

    for (Job &job : jobs_)
    {
        if (job.active)
            continue;
        job.active = true;
        job.id = nextId_++;
        job.label = label;
        job.command = command;
        job.callback = callback;
        job.clock = clock;
        job.due = due;
        job.interval = interval;
        if (persisted && !save())
        {
            job = Job{};
            --nextId_;
            return -1;
        }
        return static_cast<int32_t>(job.id);
    }
    return -1;
}

int32_t Scheduler::scheduleAtWall(const String &label, const String &command,
                                  SchedulerCallback callback, uint32_t epochSeconds)
{
    if (epochSeconds == 0)
        return -1;
    return add(label, command, SchedulerClock::Wall, epochSeconds, 0, callback);
}

int32_t Scheduler::scheduleAfter(const String &label, const String &command,
                                 SchedulerCallback callback, uint32_t delayMs)
{
    JobGuard guard;
    if (delayMs > MaxInterval)
        return -1;
    return add(label, command, SchedulerClock::Monotonic, millis() + delayMs, 0, callback);
}

int32_t Scheduler::scheduleEveryWall(const String &label, const String &command,
                                     SchedulerCallback callback, uint32_t intervalSeconds)
{
    JobGuard guard;
    if (intervalSeconds == 0 || intervalSeconds > MaxInterval)
        return -1;
    ensureInitialized();
    // A zero due time starts the interval when the wall clock first becomes valid.
    const uint32_t due = wallTimeReady() ? NightMare::Time::now() + intervalSeconds : 0;
    return add(label, command, SchedulerClock::Wall, due, intervalSeconds, callback);
}

int32_t Scheduler::scheduleEveryMonotonic(const String &label, const String &command,
                                          SchedulerCallback callback, uint32_t intervalMs)
{
    JobGuard guard;
    if (intervalMs == 0 || intervalMs > MaxInterval)
        return -1;
    return add(label, command, SchedulerClock::Monotonic, millis() + intervalMs, intervalMs,
               callback);
}

int32_t Scheduler::atWall(const String &label, const String &command, uint32_t epochSeconds)
{
    return scheduleAtWall(label, command, nullptr, epochSeconds);
}

int32_t Scheduler::atWall(const String &label, SchedulerCallback callback, uint32_t epochSeconds)
{
    return scheduleAtWall(label, String(), callback, epochSeconds);
}

int32_t Scheduler::after(const String &label, const String &command, uint32_t delayMs)
{
    return scheduleAfter(label, command, nullptr, delayMs);
}

int32_t Scheduler::after(const String &label, SchedulerCallback callback, uint32_t delayMs)
{
    return scheduleAfter(label, String(), callback, delayMs);
}

int32_t Scheduler::everyWall(const String &label, const String &command, uint32_t intervalSeconds)
{
    return scheduleEveryWall(label, command, nullptr, intervalSeconds);
}

int32_t Scheduler::everyWall(const String &label, SchedulerCallback callback, uint32_t intervalSeconds)
{
    return scheduleEveryWall(label, String(), callback, intervalSeconds);
}

int32_t Scheduler::everyMonotonic(const String &label, const String &command, uint32_t intervalMs)
{
    return scheduleEveryMonotonic(label, command, nullptr, intervalMs);
}

int32_t Scheduler::everyMonotonic(const String &label, SchedulerCallback callback, uint32_t intervalMs)
{
    return scheduleEveryMonotonic(label, String(), callback, intervalMs);
}

int32_t Scheduler::timer(const String &label, SchedulerCallback callback, uint32_t intervalMs)
{
    return everyMonotonic(label, callback, intervalMs);
}

int32_t Scheduler::setTimeout(SchedulerCallback callback, uint32_t delayMs)
{
    JobGuard guard;
    return after(String("Timeout_") + String(nextTimeoutId_++), callback, delayMs);
}

bool Scheduler::remove(const String &label)
{
    JobGuard guard;
    ensureInitialized();
    for (Job &job : jobs_)
    {
        if (!job.active || job.label != label)
            continue;
        const bool persisted = persists(job);
        const Job previous = job;
        job = Job{};
        if (persisted && !save())
        {
            job = previous;
            return false;
        }
        return true;
    }
    return false;
}

bool Scheduler::remove(uint32_t id)
{
    JobGuard guard;
    ensureInitialized();
    for (Job &job : jobs_)
    {
        if (!job.active || job.id != id)
            continue;
        const bool persisted = persists(job);
        const Job previous = job;
        job = Job{};
        if (persisted && !save())
        {
            job = previous;
            return false;
        }
        return true;
    }
    return false;
}

bool Scheduler::clear()
{
    JobGuard guard;
    ensureInitialized();
    if (!storageReady_)
        return false;

    File file = LittleFS.open(JobsFile, "w");
    if (!file)
        return false;
    constexpr char EmptyJobs[] = "{\"jobs\":[]}";
    const bool written = file.print(EmptyJobs) == sizeof(EmptyJobs) - 1;
    file.close();
    if (!written)
        return false;
    for (Job &job : jobs_)
        job = Job{};
    return true;
}

String Scheduler::list()
{
    JobGuard guard;
    ensureInitialized();

    DynamicJsonDocument doc(32768);
    JsonArray array = doc.createNestedArray("jobs");
    uint8_t count = 0;
    for (const Job &job : jobs_)
    {
        if (!job.active)
            continue;
        ++count;
        JsonObject item = array.createNestedObject();
        item["id"] = job.id;
        item["label"] = job.label;
        item["clock"] = job.clock == SchedulerClock::Wall ? "wall" : "monotonic";
        item["due"] = job.due;
        item["interval"] = job.interval;
        item["repeat"] = job.interval != 0;
        // A callback's address means nothing to a reader, so only its kind is shown.
        if (job.callback != nullptr)
            item["target"] = "callback";
        else
        {
            item["target"] = "command";
            item["command"] = job.command;
        }
    }
    doc["count"] = count;
    const bool wallReady = wallTimeReady();
    doc["wallReady"] = wallReady;
    doc["wallNow"] = wallReady ? NightMare::Time::now() : 0;
    doc["monotonicMs"] = millis();
    if (doc.overflowed())
        return String();
    String output;
    serializeJson(doc, output);
    return output;
}

void Scheduler::tick()
{
    JobGuard guard;
    if (dispatching_)
        return;
    ensureInitialized();

    dispatching_ = true;
    const uint32_t monotonicNow = millis();
    const bool wallReady = wallTimeReady();
    const uint32_t wallNow = wallReady ? NightMare::Time::now() : 0;
    const uint32_t lastExistingId = nextId_ - 1;
    for (Job &job : jobs_)
    {
        if (!job.active || job.id > lastExistingId)
            continue;

        if (job.clock == SchedulerClock::Wall)
        {
            if (!wallReady)
                continue;
            if (job.due == 0 && job.interval != 0)
            {
                job.due = wallNow + job.interval;
                continue;
            }
            if (wallNow < job.due)
                continue;
        }
        else if (static_cast<int32_t>(monotonicNow - job.due) < 0)
        {
            continue;
        }

        const bool persistRemoval = persists(job) && job.interval == 0;
        const Job previous = job;
        if (job.interval == 0)
            job = Job{};
        else if (job.clock == SchedulerClock::Wall)
            job.due = wallNow + job.interval;
        else
            job.due = monotonicNow + job.interval;

        if (persistRemoval && !save())
        {
            job = previous;
            continue;
        }

        // The live slot may already be empty (a one-shot), and the job may
        // remove or replace itself while unlocked, so only the copy is used.
        guard.unlock();
        if (previous.callback != nullptr)
            previous.callback();
        else
            handleNightMareCommand(previous.command,
                                   NightmareContext(NM_CMD_SRC_JOB, String(previous.id)));
        guard.lock();
    }
    dispatching_ = false;
}

bool Scheduler::save()
{
    if (!storageReady_)
        return false;

    DynamicJsonDocument doc(32768);
    JsonArray array = doc.createNestedArray("jobs");
    for (const Job &job : jobs_)
    {
        if (!persists(job))
            continue;
        JsonObject item = array.createNestedObject();
        item["id"] = job.id;
        item["label"] = job.label;
        item["command"] = job.command;
        item["due"] = job.due;
        item["interval"] = job.interval;
    }
    if (doc.overflowed())
        return false;
    File file = LittleFS.open(JobsFile, "w");
    if (!file)
        return false;
    size_t written = serializeJson(doc, file);
    file.close();
    return written != 0;
}

bool Scheduler::load()
{
    // No file simply means no persisted jobs.
    if (!LittleFS.exists(JobsFile))
        return true;
    File file = LittleFS.open(JobsFile, "r");
    if (!file)
        return false;
    DynamicJsonDocument doc(32768);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error)
        return false;

    for (JsonObject item : doc["jobs"].as<JsonArray>())
    {
        String label = item["label"].as<String>();
        String command = item["command"].as<String>();
        uint32_t due = item["due"] | 0UL;
        uint32_t interval = item["interval"] | 0UL;
        uint32_t id = item["id"] | 0UL;
        if (label.length() == 0 || label.length() > MaxLabelLength ||
            command.length() == 0 || command.length() > NM_MAX_MESSAGE_LEN ||
            (due == 0 && interval == 0))
            continue;
        bool duplicateLabel = false;
        bool duplicateId = false;
        for (const Job &job : jobs_)
        {
            duplicateLabel |= job.active && job.label == label;
            duplicateId |= job.active && job.id == id;
        }
        if (duplicateLabel)
            continue;
        if (id == 0 || id > INT32_MAX || duplicateId)
            id = nextId_;
        if (id == 0 || id > INT32_MAX)
            continue;
        for (Job &job : jobs_)
        {
            if (job.active)
                continue;
            job.active = true;
            job.id = id;
            job.label = label;
            job.command = command;
            job.clock = SchedulerClock::Wall;
            job.due = due;
            job.interval = interval;
            if (nextId_ <= id)
                nextId_ = id + 1;
            break;
        }
    }
    return true;
}
#endif // NM_ENABLE_SCHEDULER
