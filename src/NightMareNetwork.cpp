#include "NightMareNetwork.h"

template <typename T>
ServerVariable<T>::ServerVariable(T initialValue)
{
    value = initialValue;
}
template <typename T>
ServerVariable<T>::ServerVariable()
{
}
template <typename T>
void ServerVariable<T>::handleServer(T newServerValue)
{
    serverValue = newServerValue;
    stale = false;
    millis_last_server_recieved = millis();
    if (assert_handled)
    {
        value = newServerValue;
        onValueChanged();
    }
}
template <typename T>
void ServerVariable<T>::sync()
{
    if (millis() - millis_last_server_recieved > millis_to_stale)
    {
        stale = true;
        onValueChanged();
    }
    if (millis() > millis_to_assert_server && !assert_handled)
    {
        bool equal = value == serverValue;
        if (!equal)
        {
            value = serverValue;
            onValueChanged();
        }
        if (on_assert_result)
        {
            (*on_assert_result)(equal);
        }
        assert_handled = true;
    }
    if (!send_delay)
        return;
    if (millis() > millis_to_send && !send_handled)
    {
        if (on_send)
        {
            (*on_send)(value);
            Serial.printf("[%d] delay ended\n", millis());
        }
        send_handled = true;
    }
}
template <typename T>
void ServerVariable<T>::change(T newValue)
{
    value = newValue;
    if (send_delay)
    {
        millis_to_send = millis() + millis_to_delay;
        send_handled = false;
        Serial.printf("[%d] delay Started\n", millis());
    }

    millis_to_assert_server = millis() + ASSERT_DELAY;
    assert_handled = false;
    onValueChanged();
}
template <typename T>
void ServerVariable<T>::onValueChanged()
{
    if (on_value_changed)
    {
        (*on_value_changed)();
    }
}
#ifdef USE_INT_TEMPLATE
template class ServerVariable<int>;
#endif
#ifdef USE_UINT32_TEMPLATE
template class ServerVariable<uint32_t>;
#endif
#ifdef USE_UINT16_TEMPLATE
template class ServerVariable<uint32_t>;
#endif
#ifdef USE_BYTE_TEMPLATE
template class ServerVariable<Byte>;
#endif
#ifdef USE_DOUBLE_TEMPLATE
template class ServerVariable<double>;
#endif
#ifdef USE_STRING_TEMPLATE
template class ServerVariable<String>;
#endif
#ifdef USE_BOOL_TEMPLATE
template class ServerVariable<bool>;
#endif

String formatString(const char *format, ...)
{
    // Create a buffer to store the formatted string
    char buffer[FORMAT_BUFFER_SIZE]; // You can adjust the size as needed

    va_list args;
    va_start(args, format);

    // Format the string into the buffer
    int len = vsnprintf(buffer, sizeof(buffer), format, args);

    va_end(args);

    if (len < 0)
    {
        // Error occurred during formatting
        return String();
    }

    // Convert the formatted buffer to a String
    return String(buffer);
}

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

void TimerTask::reset()
{
    label = "unused";
    enable = false;
    use_millis = false;
    interval = 100;
    last_time = 0;
    callback = NULL;
}

bool TimersHandler::create(String label, uint16_t interval, void (*callback)(void), bool use_millis = false)
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

void TimersHandler::run()
{
    for (size_t i = 0; i < MAX_TASKS; i++)
    {
        _tasks[i].run();
    }
}