#pragma once
#include <Modules.config.h>
#ifdef COMPILE_SCHEDULER
#include <Arduino.h>
#include <TimeLib.h>
#include <ArduinoJson.h>
#define MAX_SCHEDULER_TASKS 10


struct SchedulerTask
{
    u_int32_t id;
    bool repeat = false;
    uint32_t interval = 0;
    bool armed = false;
    String command;
    uint32_t executionTime;
};

// Usage:
// Scheduler scheduler;
// on setup():
//  - scheduler.onCommand(your_function_to_run_commands);
// on loop():
//  - scheduler.run();

class Scheduler
{
    private:
    SchedulerTask tasks[MAX_SCHEDULER_TASKS];
    uint8_t currentTasks = 0;
    uint8_t nextTaskIndex = 0;
    void (*runCmd)(String cmd);
public:
    Scheduler();
    void onCommand(void (*runCommand)(String cmd));
    uint32_t add(String cmd, uint32_t timestamp);
    void run();
    void clear();
    SchedulerTask* getByID(uint16_t id);
    bool killByID(uint16_t id);
    String listTasks();
};

extern Scheduler scheduler;

#endif