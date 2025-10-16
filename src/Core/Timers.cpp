#include <Core/Timers.h>
#ifdef COMPILE_TIMERS
/// @brief Creates the Timerhandler
TimersHandler Timers = TimersHandler();

/// @brief Runs the task, should be called periodically.
void TimerTask::run()
{
    if (!enable)
        return;

    uint32_t _now = millis();
    if (!use_millis)
        _now = now();

    if (_now - last_time >= interval)
    {
        last_time = _now;
        if (callback)
        {
            (*callback)();
        }
    }
}

/// @brief Resets the Timer task
void TimerTask::reset()
{
    label = "unused";
    enable = false;
    use_millis = false;
    interval = 100;
    last_time = 0;
    callback = NULL;
}

/// @brief Gets how many ticks {ms or s} are left for the task to be called.
/// @return how many {ms or s} are left for the task to be called again.
uint16_t TimerTask::timeLeft()
{
    uint32_t _now = now();
    if (use_millis)
        _now = millis();

    return _now - last_time;
}

/// @brief Create a task to be run and adds it to the array of the Handler
/// @param label The label of the task
/// @param interval The interval to run the 
/// @param callback The function to be called each interval.
/// @param use_millis True to use milliseconds, false to use in seconds.
/// @return True if the timer was created.
bool TimersHandler::create(String label, uint16_t interval, void (*callback)(void), bool use_millis)
{
    indexresult res = getIndex(label);
    if (res.full || res.found)
        return false;
    if (res.index < MAX_TASKS)
    {
        _tasks[res.index].callback = callback;
        _tasks[res.index].label = label;
        _tasks[res.index].interval = interval;
        _tasks[res.index].use_millis = use_millis;
        _tasks[res.index].enable = true;
        Serial.printf("...timer [%s] created at [%d]...\n", label.c_str(), res.index);
        return true;
    }
    return false;
}

/// @brief Gets the index of a task by its label.
/// @param label The label of the task that we want to find.
/// @return a `indexresult` object containg  the index of the label,
/// if the array is full or the next free index;
indexresult TimersHandler::getIndex(String label)
{
    indexresult res;
    for (size_t i = 0; i < MAX_TASKS; i++)
    {
        if (_tasks[i].label == label)
        {
            res.found = true;
            res.index = i;
            return res;
        }
    }
    res.full = true;
    for (size_t i = 0; i < MAX_TASKS; i++)
    {
        if (_tasks[i].label == "unused")
        {
            res.index = i;
            res.full = false;
            res.found = false;
            return res;
        }
    }
    return res;
}

/// @brief Runs all the tasks should be called periodically
void TimersHandler::run()
{
    for (size_t i = 0; i < MAX_TASKS; i++)
    {
        _tasks[i].run();
    }
}

/// @brief Gets the time left on a specific task
/// @param label The label of the task that we want to find how many time is left.
/// @return A String with the time left of the task with the label.
String TimersHandler::Timeleft(String label)
{
    indexresult res = getIndex(label);
    if (res.found)
        return String(_tasks[res.index].timeLeft());

    return "There is no task with such label";
}
#endif