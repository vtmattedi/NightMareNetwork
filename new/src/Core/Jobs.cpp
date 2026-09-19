#include "Jobs.h"

#include "NightMareCommand.h"
#include "StateStore.h"
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <TimeLib.h>
#include <freertos/semphr.h>

namespace
{
constexpr const char *JobsFile = "/jobs.json";
constexpr const char *LegacySchedulerFile = "/scheduleTasks.json";
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
    return SystemState.getFlag("time_synced");
}
}

JobManager gScheduler;

bool JobManager::begin()
{
    JobGuard guard;
    if (begun_)
    {
        if (!storageReady_ && static_cast<int32_t>(millis() - nextStorageRetry_) >= 0)
        {
            storageReady_ = PersistentSettings.begin();
            nextStorageRetry_ = millis() + StorageRetryMs;
            if (storageReady_)
                load();
        }
        if (schedulerTask_ == nullptr)
            startScheduler();
        return storageReady_;
    }

    begun_ = true;
    storageReady_ = PersistentSettings.begin();
    nextStorageRetry_ = millis() + StorageRetryMs;
    if (storageReady_)
        load();
    startScheduler();
    return storageReady_;
}

bool JobManager::startScheduler(uint8_t priority, uint32_t stackSize)
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

void JobManager::task(void *context)
{
    JobManager *manager = static_cast<JobManager *>(context);
    const TickType_t interval = pdMS_TO_TICKS(SchedulerPollMs) > 0
                                    ? pdMS_TO_TICKS(SchedulerPollMs)
                                    : 1;
    for (;;)
    {
        manager->tick();
        vTaskDelay(interval);
    }
}

int32_t JobManager::add(const String &label, const String &command, JobClock clock,
                        uint32_t due, uint32_t interval)
{
    JobGuard guard;
    if (!begun_ || (clock == JobClock::Wall && !storageReady_))
        begin();
    if (label.length() == 0 || label.length() > MaxLabelLength ||
        command.length() == 0 || command.length() > NM_MAX_MESSAGE_LEN)
        return -1;
    if (clock == JobClock::Wall && !storageReady_)
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
        job.clock = clock;
        job.due = due;
        job.interval = interval;
        if (clock == JobClock::Wall && !save())
        {
            job = Job{};
            --nextId_;
            return -1;
        }
        return static_cast<int32_t>(job.id);
    }
    return -1;
}

int32_t JobManager::atWall(const String &label, const String &command, uint32_t epochSeconds)
{
    if (epochSeconds == 0)
        return -1;
    return add(label, command, JobClock::Wall, epochSeconds, 0);
}

int32_t JobManager::after(const String &label, const String &command, uint32_t delayMs)
{
    JobGuard guard;
    if (delayMs > MaxInterval)
        return -1;
    if (!begun_)
        begin();
    return add(label, command, JobClock::Monotonic, millis() + delayMs, 0);
}

int32_t JobManager::everyWall(const String &label, const String &command, uint32_t intervalSeconds)
{
    JobGuard guard;
    if (intervalSeconds == 0 || intervalSeconds > MaxInterval)
        return -1;
    if (!begun_)
        begin();
    // A zero due time starts the interval when the wall clock first becomes valid.
    uint32_t due = wallTimeReady() ? now() + intervalSeconds : 0;
    return add(label, command, JobClock::Wall, due, intervalSeconds);
}

int32_t JobManager::everyMonotonic(const String &label, const String &command, uint32_t intervalMs)
{
    JobGuard guard;
    if (intervalMs == 0 || intervalMs > MaxInterval)
        return -1;
    if (!begun_)
        begin();
    return add(label, command, JobClock::Monotonic, millis() + intervalMs, intervalMs);
}

bool JobManager::remove(const String &label)
{
    JobGuard guard;
    if (!begun_ || !storageReady_)
        begin();
    for (Job &job : jobs_)
    {
        if (!job.active || job.label != label)
            continue;
        bool persisted = job.clock == JobClock::Wall;
        Job previous = job;
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

bool JobManager::remove(uint32_t id)
{
    JobGuard guard;
    if (!begun_ || !storageReady_)
        begin();
    for (Job &job : jobs_)
    {
        if (!job.active || job.id != id)
            continue;
        bool persisted = job.clock == JobClock::Wall;
        Job previous = job;
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

bool JobManager::clear()
{
    JobGuard guard;
    if (!begun_ || !storageReady_)
        begin();
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

String JobManager::list()
{
    JobGuard guard;
    if (!begun_ || !storageReady_)
        begin();

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
        item["command"] = job.command;
        item["clock"] = job.clock == JobClock::Wall ? "wall" : "monotonic";
        item["due"] = job.due;
        item["interval"] = job.interval;
        item["repeat"] = job.interval != 0;
    }
    doc["count"] = count;
    const bool wallReady = wallTimeReady();
    doc["wallReady"] = wallReady;
    doc["wallNow"] = wallReady ? now() : 0;
    doc["monotonicMs"] = millis();
    if (doc.overflowed())
        return String();
    String output;
    serializeJson(doc, output);
    return output;
}

void JobManager::tick()
{
    JobGuard guard;
    if (dispatching_)
        return;
    if (!begun_ || !storageReady_)
        begin();

    dispatching_ = true;
    const uint32_t monotonicNow = millis();
    const bool wallReady = wallTimeReady();
    const uint32_t wallNow = wallReady ? now() : 0;
    const uint32_t lastExistingId = nextId_ - 1;
    for (Job &job : jobs_)
    {
        if (!job.active || job.id > lastExistingId)
            continue;

        if (job.clock == JobClock::Wall)
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

        const bool persistRemoval = job.clock == JobClock::Wall && job.interval == 0;
        const Job previous = job;
        if (job.interval == 0)
            job = Job{};
        else if (job.clock == JobClock::Wall)
            job.due = wallNow + job.interval;
        else
            job.due = monotonicNow + job.interval;

        if (persistRemoval && !save())
        {
            job = previous;
            continue;
        }

        guard.unlock();
        handleNightMareCommand(previous.command,
                               NightmareContext(NM_CMD_SRC_JOB, String(previous.id)));
        guard.lock();
    }
    dispatching_ = false;
}

bool JobManager::save()
{
    if (!storageReady_)
        return false;

    DynamicJsonDocument doc(32768);
    JsonArray array = doc.createNestedArray("jobs");
    for (const Job &job : jobs_)
    {
        if (!job.active || job.clock != JobClock::Wall)
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

bool JobManager::load()
{
    if (!LittleFS.exists(JobsFile))
        return importLegacy();
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
            job.clock = JobClock::Wall;
            job.due = due;
            job.interval = interval;
            if (nextId_ <= id)
                nextId_ = id + 1;
            break;
        }
    }
    return true;
}

bool JobManager::importLegacy()
{
    if (!LittleFS.exists(LegacySchedulerFile))
        return true;
    File file = LittleFS.open(LegacySchedulerFile, "r");
    if (!file)
        return false;
    DynamicJsonDocument doc(32768);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error)
        return false;

    for (JsonObject item : doc["tasks"].as<JsonArray>())
    {
        String label = item["label"].as<String>();
        String command = item["command"].as<String>();
        uint32_t due = item["executionTime"] | 0UL;
        uint32_t interval = item["interval"] | 0UL;
        bool synced = item["executionTimeSynced"] | false;
        if (!synced)
        {
            if (interval == 0)
                continue; // The old boot-relative one-shot deadline cannot be recovered.
            due = 0; // Start a fresh wall interval after synchronization.
        }
        if (label.length() == 0 || label.length() > MaxLabelLength ||
            command.length() == 0 || command.length() > NM_MAX_MESSAGE_LEN ||
            (due == 0 && interval == 0))
            continue;
        bool duplicateLabel = false;
        for (const Job &job : jobs_)
            duplicateLabel |= job.active && job.label == label;
        if (duplicateLabel || nextId_ > INT32_MAX)
            continue;
        for (Job &job : jobs_)
        {
            if (job.active)
                continue;
            job.active = true;
            job.id = nextId_++;
            job.label = label;
            job.command = command;
            job.clock = JobClock::Wall;
            job.due = due;
            job.interval = interval;
            break;
        }
    }
    return save();
}
