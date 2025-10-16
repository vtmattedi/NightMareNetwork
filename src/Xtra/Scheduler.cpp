#include "Scheduler.h"
#ifdef COMPILE_SCHEDULER
Scheduler scheduler;
static uint16_t taskID = 0;

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
            Serial.printf("\x1b[96;1m[Scheduler]\x1b[0m Added task ID %d: '%s' at %u\n", tasks[i].id, cmd.c_str(), timestamp);
            return tasks[i].id;
        }
    }
    return -1; // Indicate failure to add task
}
/// @brief Retrieves a scheduled task by its ID.
/// @param id The ID of the task to retrieve.
/// @return A pointer to the scheduled task, or nullptr if not found.
SchedulerTask* Scheduler::getByID(uint16_t id)
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

bool Scheduler::killByID(uint16_t id)
{
    for (uint8_t i = 0; i < MAX_SCHEDULER_TASKS; i++)
    {
        if (tasks[i].armed && tasks[i].id == id)
        {
            tasks[i].armed = false;
            currentTasks--;
            Serial.printf("\x1b[91;1m[Scheduler]\x1b[0m Killed task ID %d: '%s'\n", tasks[i].id, tasks[i].command.c_str());
            return true;
        }
    }
    return false; // Indicate task not found
}

void Scheduler::run()
{
    if (currentTasks == 0)
        return;

    uint32_t nowTime = now();
    for (uint8_t i = 0; i < MAX_SCHEDULER_TASKS; i++)
    {
        if (tasks[i].armed && tasks[i].executionTime <= nowTime)
        {
            Serial.printf("\x1b[93;1m[Scheduler]\x1b[0m Executing task ID %d: '%s' scheduled for %u (now: %u)\n", tasks[i].id, tasks[i].command.c_str(), tasks[i].executionTime, nowTime);
            // Execute the command
            if (runCmd)
            {
                runCmd(tasks[i].command);
            }
            
            if (tasks[i].repeat && tasks[i].interval > 0)
            {
                Serial.printf("\x1b[96;1m[Scheduler]\x1b[0m Rescheduling task ID %d: '%s' to %u\n", tasks[i].id, tasks[i].command.c_str(), nowTime + tasks[i].interval);
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

void Scheduler::clear()
{
    for (uint8_t i = 0; i < MAX_SCHEDULER_TASKS; i++)
    {
        tasks[i].armed = false;
    }
    currentTasks = 0;
}
#endif