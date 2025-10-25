#include <Core/Timers.h>
#ifdef COMPILE_TIMERS
/// @brief Creates the Timerhandler
TimersHandler Timers = TimersHandler();

#ifdef COMPILE_SERIAL
#define TIMER_LOGF(fmt, ...) Serial.printf("%s%s " fmt "\n", MILLIS_LOG, TIMER_LOG, ##__VA_ARGS__)
#define TIMER_ERRORF(fmt, ...) Serial.printf("%s%s " fmt "\n", ERR_LOG, TIMER_LOG, ##__VA_ARGS__)
#else
#define TIMER_LOGF(fmt, ...)
#define TIMER_ERRORF(fmt, ...)
#endif

/// @brief Runs the task, should be called periodically.
void TimerTask::run()
{
    if (!enable)
        return;

    uint32_t _now = use_millis ? millis() : now();

    if (_now - last_time >= interval)
    {
        last_time = _now;
        if (callback)
        {
            (*callback)();
        }
        if (is_timeout)
        {
            reset();
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
    is_timeout = false;
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
        _tasks[res.index].is_timeout = false;
        TIMER_LOGF("Task '%s' Created at [%d] every %d %s", label.c_str(), res.index, interval, use_millis ? "ms" : "s");
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

indexresult TimersHandler::getIndex()
{
    indexresult res;
    res.full = true;
    res.found = false;
    for (size_t i = 0; i < MAX_TASKS; i++)
    {
        if (_tasks[i].label == "unused")
        {
            res.index = i;
            res.full = false;
            res.found = true;
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

bool TimersHandler::setTimeout(void (*callback)(void), uint16_t interval, bool use_millis)
{
    indexresult res = getIndex();
    if (res.full)
    {
        TIMER_ERRORF("could not create timeout, array full");
        return false;
    }
    if (res.index < MAX_TASKS)
    {
        _tasks[res.index].callback = callback;
        _tasks[res.index].label = "timeout_" + String(res.index);
        _tasks[res.index].interval = interval;
        _tasks[res.index].use_millis = use_millis;
        _tasks[res.index].enable = true;
        _tasks[res.index].is_timeout = true;
        _tasks[res.index].last_time = use_millis ? millis() : now();
        TIMER_LOGF("Timeout Created at [%d] in %d %s", res.index, interval, use_millis ? "ms" : "s");
        return true;
    }
    return false;
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