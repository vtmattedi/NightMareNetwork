#include "Scheduler.h"
#ifdef COMPILE_SCHEDULER
#ifdef USE_NIGHTMARE_COMMAND
#include <Xtra/NightMareCommand.h>
#endif
Scheduler scheduler;
static uint16_t taskID = 0;
#define COMPILE_SERIAL
#ifdef SCHEDULER_USE_MILLIS
#define GET_TIME() millis()
#else
#define GET_TIME() now()
#endif

#ifdef COMPILE_SERIAL
#define SCHEDULER_LOGF(fmt, ...) Serial.printf("%s " fmt, SCHEDULER_TAG, ##__VA_ARGS__)
#define SCHEDULER_ERRORF(fmt, ...) Serial.printf("%s%s " fmt, ERR_TAG, SCHEDULER_TAG, ##__VA_ARGS__)
#else
#define SCHEDULER_LOGF(fmt, ...)
#define SCHEDULER_ERRORF(fmt, ...)
#endif

/// @brief Constructor for the Scheduler class.
Scheduler::Scheduler()
{
    currentTasks = 0;
    runCmd = nullptr;
    logResult = nullptr;
    bool res = loadPersistentTasks(); // Load tasks from persistent storage on initialization
    res ? SCHEDULER_LOGF("Scheduler initialized with %d tasks loaded from persistent storage\n", currentTasks) : SCHEDULER_LOGF("Scheduler initialized with no tasks loaded from persistent storage\n");
}

/// @brief Sets the function to be called when a scheduled command is executed.
void Scheduler::onCommand(void (*runCommand)(String cmd))
{
    if (runCommand)
        runCmd = runCommand;
}

/// @brief Adds a new task to the scheduler.
/// @param label An optional label for the task.
/// @param cmd The command to be executed.
/// @param interval_seconds The interval in seconds for repeating the task. If 0, the task will not repeat.
/// @param executionTime The time at which the command should be executed.
/// @param repeat Whether the task should be repeated.
/// @return The ID of the scheduled task, or -1 if the task could not be added.
int32_t Scheduler::addTask(String label, String cmd, uint32_t interval_seconds, uint32_t executionTime, bool repeat, bool skipSave)
{

    // Check if there's space for a new task
    // Assign the task to the next available slot
    for (uint8_t i = 0; i < MAX_SCHEDULER_TASKS; i++)
    {
        if (!tasks[i].armed)
        {
            tasks[i].armed = true;
            tasks[i].command = cmd;
            tasks[i].label = label;
            tasks[i].executionTime = executionTime > 0 ? executionTime : GET_TIME() + interval_seconds;
            Serial.printf("[%lu(%s)]et: %lu (%s)\n", executionTime, TIME_FULL_STR(executionTime), tasks[i].executionTime, TIME_FULL_STR(tasks[i].executionTime));
            tasks[i].id = taskID++;
            tasks[i].repeat = repeat;
#ifdef SCHEDULER_USE_MILLIS
            interval_seconds *= 1000; // Convert seconds to milliseconds if using millis()
#endif
            tasks[i].interval = interval_seconds;
            tasks[i].executionTimeSynced = this->timeSynced; // Mark as synced if the system time has been synced
            currentTasks++;
            SCHEDULER_LOGF("Added task ID %d: '%s' at %lu (%s), every %u s \n",
                           tasks[i].id,
                           cmd.c_str(),
                           executionTime,
                           TIME_FULL_STR(executionTime),
                           tasks[i].interval);
            if (repeat && !skipSave)
            {
                savePersistentTasks("New Task Added"); // Save tasks to persistent storage if it's a repeating task
            } // Save tasks to persistent storage
            return tasks[i].id;
        }
    }
    SCHEDULER_ERRORF("Failed to add task: '%s' - Scheduler full\n", cmd.c_str());
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

/// @brief Retrieves a scheduled task by its label.
/// @param label The label of the task to retrieve.
/// @return A pointer to the scheduled task, or nullptr if not found.
SchedulerTask *Scheduler::getByLabel(String label)
{
    for (uint8_t i = 0; i < MAX_SCHEDULER_TASKS; i++)
    {
        if (tasks[i].armed && tasks[i].label == label)
        {
            return &tasks[i];
        }
    }
    return nullptr; // Indicate task not found
}

/// @brief Retrieves the ID of a scheduled task by its label.
/// @param label The label of the task to retrieve.
/// @return The ID of the task, or -1 if not found.
int32_t Scheduler::getTaskIdByLabel(String label)
{
    SchedulerTask *task = getByLabel(label);
    if (task)
    {
        return task->id;
    }
    return -1; // Indicate task not found
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

/// @brief Synchronizes a specific task's execution time based on the system time synchronization.
/// @param task The pointer to the task to synchronize. Must be a valid, armed task.
/// @param oldTime The previous time before synchronization. If not provided, it defaults to 0.
void Scheduler::syncTask(SchedulerTask *task, uint32_t oldTime)
{
    if (task)
    {
        if (task->executionTimeSynced || !task->armed)
            return;
        task->executionTime = GET_TIME() + (task->executionTime - oldTime);
        task->executionTimeSynced = true;
        if (task->repeat)
            this->savePersistentTasks("Task synchronized"); // Save the updated tasks to persistent storage
    }
    else
    {
        SCHEDULER_ERRORF("Invalid task pointer at:%p for sync\n", task);
    }
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
            if (this->timeSynced && !tasks[i].executionTimeSynced)
            {
                // If time has been synced and the task's execution time hasn't been adjusted, adjust it now
                tasks[i].executionTime = nowTime + (tasks[i].executionTime - nowTime);
                tasks[i].executionTimeSynced = true; // Mark as adjusted
            }

#ifdef USE_NIGHTMARE_COMMAND
            NightMareResults res = handleNightMareCommand(tasks[i].command, {NM_CMD_SRC_SCHEDULER, String(tasks[i].id), nullptr, false});
            res.response.replace("\n", "\n\t\t");
            if (logResult)
            {
                logResult(res);
            }
            SCHEDULER_LOGF("Task ID %d executed:\n\t<\x1b[90m%s\x1b[0m>%s\n\t\t%s\n", tasks[i].id, tasks[i].command.c_str(), OK_LOG(res.result), res.response.c_str());
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
                this->savePersistentTasks("task rescheduled"); // Save the updated tasks to persistent storage
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
    this->loadPersistentTasks(); // Reload tasks from persistent storage to ensure we have the latest state
#ifdef SCHEDULER_USE_MILLIS
    return; // No adjustment needed when using millis()
#endif
    // unsigned int timeDiff = GET_TIME() - oldTime;
    // When time is synced, we need to adjust old tasks execution times
    SCHEDULER_LOGF("Adjusting scheduled tasks for time sync. Old time: %u, New time: %u\n", oldTime, GET_TIME());
    this->timeSynced = true; // Mark that time has been synced
    for (uint8_t i = 0; i < MAX_SCHEDULER_TASKS; i++)
    {
        if (tasks[i].armed && !tasks[i].executionTimeSynced)
        {
            this->syncTask(&tasks[i], oldTime);
        }
    }
}

/// @brief Clears all scheduled tasks.
void Scheduler::clear()
{
    for (uint8_t i = 0; i < MAX_SCHEDULER_TASKS; i++)
    {
        tasks[i].command = "";
        tasks[i].label = "";
        tasks[i].armed = false;
    }
    bool res = LittleFS.remove(SCHEDULER_FILE_NAME); // Remove the persistent storage file
    if (!res)
    {
        SCHEDULER_ERRORF("Failed to remove persistent tasks file: %s\n", SCHEDULER_FILE_NAME);
    }
    currentTasks = 0;
}

/// @brief Saves the current persistent tasks to a JSON file in the LittleFS filesystem.
/// @return // True if the tasks were saved successfully, false otherwise.
bool Scheduler::savePersistentTasks(String reason)
{
    DynamicJsonDocument doc(2048);
    JsonArray tasksArray = doc.createNestedArray("tasks");
    for (uint8_t i = 0; i < MAX_SCHEDULER_TASKS; i++)
    {
        if (tasks[i].armed)
        {
            JsonObject taskObj = tasksArray.createNestedObject();
            taskObj["label"] = tasks[i].label;
            taskObj["command"] = tasks[i].command;
            taskObj["executionTime"] = tasks[i].executionTime;
            taskObj["interval"] = tasks[i].interval;
            taskObj["executionTimeSynced"] = tasks[i].executionTimeSynced;
        }
    }
    File file = LittleFS.open(SCHEDULER_FILE_NAME, FILE_WRITE);
    if (!file)
    {
        SCHEDULER_ERRORF("Failed to open file for writing: %s\n", SCHEDULER_FILE_NAME);
        return false;
    }
    serializeJson(doc, file);
    file.close();
    SCHEDULER_LOGF("Saved persistent tasks to %s (%s)\n", SCHEDULER_FILE_NAME, reason.c_str());
    return true;
}

/// @brief Loads persistent tasks from a JSON file in the LittleFS filesystem.
/// @return True if the tasks were loaded successfully, false otherwise.
bool Scheduler::loadPersistentTasks()
{
    if (!LittleFS.exists(SCHEDULER_FILE_NAME))
    {
        SCHEDULER_LOGF("No persistent tasks file found: %s\n", SCHEDULER_FILE_NAME);
        return false;
    }
    File file = LittleFS.open(SCHEDULER_FILE_NAME, FILE_READ);
    if (!file)
    {
        SCHEDULER_ERRORF("Failed to open file for reading: %s\n", SCHEDULER_FILE_NAME);
        return false;
    }
    Serial.printf("Loading persistent tasks from %s\n", file.readString().c_str());
    file = LittleFS.open(SCHEDULER_FILE_NAME, FILE_READ);
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error)
    {
        SCHEDULER_ERRORF("Failed to parse JSON from %s: %s\n", SCHEDULER_FILE_NAME, error.c_str());
        return false;
    }

    JsonArray tasksArray = doc["tasks"].as<JsonArray>();
    for (JsonObject taskObj : tasksArray)
    {
        String label = taskObj["label"].as<String>();
        uint16_t id = taskObj["id"].as<uint16_t>();
        String command = taskObj["command"].as<String>();
        uint32_t executionTime = taskObj["executionTime"].as<uint32_t>();
        uint32_t interval = taskObj["interval"].as<uint32_t>();
        int newTaskId = addTask(label, command, interval, executionTime, true, true); // Add the task without saving again
        if (newTaskId == -1)
        {
            SCHEDULER_ERRORF("Failed to load task: '%s' from %s\n", command.c_str(), SCHEDULER_FILE_NAME);
        }
        else
        {
            SchedulerTask *task = getByID(newTaskId);
            if (task)
            {
                bool wasAdjusted = taskObj["executionTimeSynced"] | false; // Default to false if not present
                task->executionTimeSynced = wasAdjusted;
            }
            Serial.printf("Loaded task: %s (ID: %d) adjusted: %s\n", label.c_str(), newTaskId, task->executionTimeSynced ? "true" : "false");
        }

    }
    SCHEDULER_LOGF("Loaded persistent tasks from %s\n", SCHEDULER_FILE_NAME);
    return true;
}

/// @brief Deletes a scheduled task by its label, also removing it from persistent storage.
/// @param label The label of the task to delete.
/// @return true if the task was deleted, false otherwise.
bool Scheduler::deleteTaskByLabel(String label)
{
    for (uint8_t i = 0; i < MAX_SCHEDULER_TASKS; i++)
    {
        if (tasks[i].armed && tasks[i].label == label)
        {
            tasks[i].armed = false;
            currentTasks--;
            SCHEDULER_LOGF("Deleted task ID %d: '%s'\n", tasks[i].id, tasks[i].command.c_str());
            if (tasks[i].repeat)
            {
                savePersistentTasks("Task deleted"); // Save tasks to persistent storage if it was a repeating task
            }
            return true;
        }
    }
    return false; // Indicate task not found
}

/// @brief Deletes a scheduled task by its ID, also removing it from persistent storage.
/// @param id The ID of the task to delete.
/// @return true if the task was deleted, false otherwise.
bool Scheduler::deleteTask(uint16_t id)
{
    for (uint8_t i = 0; i < MAX_SCHEDULER_TASKS; i++)
    {
        if (tasks[i].armed && tasks[i].id == id)
        {
            tasks[i].armed = false;
            currentTasks--;
            SCHEDULER_LOGF("Deleted task ID %d: '%s'\n", tasks[i].id, tasks[i].command.c_str());
            if (tasks[i].repeat)
            {
                savePersistentTasks("Task deleted"); // Save tasks to persistent storage if it was a repeating task
            }
            return true;
        }
    }
    return false; // Indicate task not found
}

/// @brief Checks if a task with the given ID exists in the scheduler.
/// @param id The ID of the task to check.
/// @return true if the task exists, false otherwise.
bool Scheduler::taskExists(uint16_t id)
{
    return getByID(id) != nullptr;
}

/// @brief Checks if a task with the given label exists in the scheduler.
/// @param label label The label of the task to check.
/// @return true if the task exists, false otherwise.
bool Scheduler::taskExists(String label)
{
    return getByLabel(label) != nullptr;
}

/// @brief Set the function to be called when a task result is available
/// @param logResultFunc The function to be called
void Scheduler::onLogResult(void (*logResultFunc)(NightMareResults result))
{
    if (logResultFunc)
        logResult = logResultFunc;
}
#endif