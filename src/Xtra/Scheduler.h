#pragma once
#include <Modules.config.h>
#ifdef COMPILE_SCHEDULER
#include <Arduino.h>
#include <TimeLib.h>
#include <ArduinoJson.h>
#define MAX_SCHEDULER_TASKS 10

#ifdef USE_NIGHTMARE_COMMAND
#include <Xtra/NightMareTypes.h>
#endif

#define SCHEDULER_FILE_NAME "/scheduleTasks.json"
// #define SCHEDULER_USE_MILLIS //Use millis() instead of now() for scheduling tasks, useful if you don't have time sync

struct SchedulerTask
{
    String label; // Optional label for the task
    uint32_t id;
    bool repeat = false;
    uint32_t interval = 0;
    bool armed = false;
    String command;
    uint32_t executionTime;
    bool executionTimeSynced = false; // Indicates if the execution time has been adjusted after a time sync
};

// Usage:
// Scheduler scheduler;
// on setup():
//  - scheduler.onCommand(your_function_to_run_commands);
//  - scheduler.begin(); // after the filesystem is mounted, to load persisted tasks
// on loop():
//  - scheduler.run();

class Scheduler
{
private:
    uint8_t currentTasks = 0;
    uint8_t nextTaskIndex = 0;
    bool savePersistentTasks(String reason);
    bool loadPersistentTasks();
    SchedulerTask tasks[MAX_SCHEDULER_TASKS];
    bool timeSynced = false; // Indicates if the system time has been synced at least once
    bool enable_scheduler_log = false;
    bool configloaded = false;
    void (*runCmd)(String cmd);
    void (*logResult)(NightMareResults result);

public:
    Scheduler();

    void onCommand(void (*runCommand)(String cmd));
    void onLogResult(void (*logResultFunc)(NightMareResults result));
    int32_t addTask(String label, String cmd, uint32_t interval_seconds, uint32_t executionTime, bool repeat = false, bool skipSave = false);
    void run();
    void clear();
    int32_t getTaskIdByLabel(String label);
    SchedulerTask *getByLabel(String label);
    SchedulerTask *getByID(uint16_t id);
    bool killByID(uint16_t id);
    bool deleteTask(uint16_t id);
    bool deleteTaskByLabel(String label);
    bool taskExists(uint16_t id);
    bool taskExists(String label);
    String listTasks(bool onlyPersistent = false);
    void syncTask(SchedulerTask *task, uint32_t oldTime = 0);
    void onSync(unsigned int oldTime = 0);
};

extern Scheduler scheduler;

#endif