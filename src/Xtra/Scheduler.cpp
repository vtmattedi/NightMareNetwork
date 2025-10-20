#include "Scheduler.h"
#ifdef COMPILE_SCHEDULER
Scheduler scheduler;
static uint16_t taskID = 0;
#ifdef SCHEDULER_USE_MILLIS
#define GET_TIME() millis()
#else
#define GET_TIME() now()
#endif

#ifdef COMPILE_SERIAL
#define SCHEDULER_LOGF(fmt, ...) Serial.printf("%s " fmt "\n", SCHEDULER_LOG, ##__VA_ARGS__)
#define SCHEDULER_ERRORF(fmt, ...) Serial.printf("%s%s " fmt "\n", ERR_LOG, SCHEDULER_LOG, ##__VA_ARGS__)
#else
#define SCHEDULER_LOGF(fmt, ...)
#define SCHEDULER_ERRORF(fmt, ...)
#endif

/// @brief Constructor for the Scheduler class.
Scheduler::Scheduler()
{
    currentTasks = 0;
    runCmd = nullptr;
}

/// @brief Sets the function to be called when a scheduled command is executed.
void Scheduler::onCommand(void (*runCommand)(String cmd))
{
    if (runCommand)
        runCmd = runCommand;
}

/// @brief Adds a new task to the scheduler.
/// @param cmd The command to be executed.
/// @param timestamp The time at which the command should be executed.
/// @return The ID of the scheduled task, or -1 if the task could not be added.
uint32_t Scheduler::add(String cmd, uint32_t timestamp)
{
    // Check if there's space for a new task
    // Assign the task to the next available slot
    for (uint8_t i = 0; i < MAX_SCHEDULER_TASKS; i++)
    {
        if (!tasks[i].armed)
        {
            tasks[i].armed = true;
            tasks[i].command = cmd;
            tasks[i].executionTime = timestamp;
            tasks[i].id = taskID++;
            tasks[i].repeat = false;
            tasks[i].interval = 0;
            currentTasks++;
            SCHEDULER_LOGF("Added task ID %d: '%s' at %u\n", tasks[i].id, cmd.c_str(), timestamp);
            return tasks[i].id;
        }
    }
    SCHEDULER_ERRORF("Failed to add task: '%s' at %u - Scheduler full\n", cmd.c_str(), timestamp);
    return -1; // Indicate failure to add task
}
/// @brief Retrieves a scheduled task by its ID.
/// @param id The ID of the task to retrieve.
/// @return A pointer to the scheduled task, or nullptr if not found.
SchedulerTask *Scheduler::getByID(uint16_t id)
{
    for (uint8_t i = 0; i < MAX_SCHEDULER_TASKS; i++)
    {
        if (tasks[i].armed && tasks[i].id == id)
        {
            return &tasks[i];
        }
    }
    return nullptr; // Indicate task not found
}

/// @brief Lists all scheduled tasks in JSON format.
/// @return A String containing the JSON representation of all scheduled tasks.
String Scheduler::listTasks()
{
    DynamicJsonDocument doc(1024);
    doc["count"] = currentTasks;
    auto tasksArray = doc.createNestedArray("tasks");
    for (uint8_t i = 0; i < MAX_SCHEDULER_TASKS; i++)
    {
        if (tasks[i].armed)
        {
            JsonObject taskObj = tasksArray.createNestedObject();
            taskObj["id"] = tasks[i].id;
            taskObj["command"] = tasks[i].command;
            taskObj["executionTime"] = tasks[i].executionTime;
            taskObj["repeat"] = tasks[i].repeat;
            taskObj["interval"] = tasks[i].interval;
        }
    }
    String output;
    serializeJson(doc, output);
    return output;
}

/// @brief Kills (removes) a scheduled task by its ID.
/// @param id // The ID of the task to kill.
/// @return // True if the task was found and killed, false otherwise.
bool Scheduler::killByID(uint16_t id)
{
    for (uint8_t i = 0; i < MAX_SCHEDULER_TASKS; i++)
    {
        if (tasks[i].armed && tasks[i].id == id)
        {
            tasks[i].armed = false;
            currentTasks--;
            SCHEDULER_LOGF("Killed task ID %d: '%s'\n", tasks[i].id, tasks[i].command.c_str());
            return true;
        }
    }
    return false; // Indicate task not found
}

/// @brief Checks and runs any tasks that are due for execution.
// This should be called regularly.
// Not that the execution of the command is done in the same context as this call.
void Scheduler::run()
{
    if (currentTasks == 0)
        return;

    uint32_t nowTime = GET_TIME();
    for (uint8_t i = 0; i < MAX_SCHEDULER_TASKS; i++)
    {
        if (tasks[i].armed && tasks[i].executionTime <= nowTime)
        {
#ifdef COMPILE_SERIAL
            Serial.printf("%s Executing task ID %d: '%s' scheduled for %u (now: %u)\n", SCHEDULER_LOG, tasks[i].id, tasks[i].command.c_str(), tasks[i].executionTime, nowTime);
#endif
#ifdef USE_NIGHTMARE_COMMAND
                NightMareResults res = handleNightMareCommand(tasks[i].command);
                SCHEDULER_LOGF("Task ID %d executed with result: %s \n%s\n", tasks[i].id, OK_LOG(res.result), res.response.c_str());
#endif
            // Execute the command
            if (runCmd)
            {
                runCmd(tasks[i].command);
            }

            if (tasks[i].repeat && tasks[i].interval > 0)
            {
                SCHEDULER_LOGF("Rescheduling task ID %d: '%s' to %u\n", tasks[i].id, tasks[i].command.c_str(), nowTime + tasks[i].interval);
                // Reschedule the task
                tasks[i].executionTime = nowTime + tasks[i].interval;
            }
            else
            {
                // Disarm the task
                tasks[i].armed = false;
                currentTasks--;
            }
        }
    }
}

/// @brief This should be called when the system time is synchronized. I.E. time is set.
/// This will adjust any scheduled tasks that were set in the past.
/// @param oldTime The previous time before synchronization.
/// If not available, will assume that the old system time was the seconds since boot.
void Scheduler::onSync(unsigned int oldTime)
{
    // unsigned int timeDiff = GET_TIME() - oldTime;
    // When time is synced, we need to adjust old tasks execution times
    SCHEDULER_LOGF("Adjusting scheduled tasks for time sync. Old time: %u, New time: %u\n", oldTime, GET_TIME());
    for (uint8_t i = 0; i < MAX_SCHEDULER_TASKS; i++)
    {
        if (tasks[i].armed)
        {
            // if a task was scheduled we adjust it:
            // new execution time = now() + (old execution time - oldTime)
            // if we do not know
            tasks[i].executionTime = GET_TIME() + (tasks[i].executionTime - oldTime);
        }
    }
}

/// @brief Clears all scheduled tasks.
void Scheduler::clear()
{
    for (uint8_t i = 0; i < MAX_SCHEDULER_TASKS; i++)
    {
        tasks[i].armed = false;
    }
    currentTasks = 0;
}
#endif