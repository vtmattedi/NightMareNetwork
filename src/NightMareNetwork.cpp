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
#ifdef  USE_INT_TEMPLATE
template class ServerVariable<int>;
#endif
#ifdef  USE_UINT32_TEMPLATE
template class ServerVariable<uint32_t>;
#endif
#ifdef  USE_UINT16_TEMPLATE
template class ServerVariable<uint32_t>;
#endif
#ifdef  USE_BYTE_TEMPLATE
template class ServerVariable<Byte>;
#endif
#ifdef  USE_DOUBLE_TEMPLATE
template class ServerVariable<double>;
#endif
#ifdef  USE_STRING_TEMPLATE
template class ServerVariable<String>;
#endif
#ifdef  USE_BOOL_TEMPLATE
template class ServerVariable<bool>;
#endif

formatString(const char *format, ...)
{
  // Create a buffer to store the formatted string
  char buffer[FORMAT_BUFFER_SIZE]; // You can adjust the size as needed

  va_list args;
  va_start(args, format);

  // Format the string into the buffer
  int len = vsnprintf(buffer, sizeof(buffer), format, args);

  va_end(args);

  if (len < 0) {
    // Error occurred during formatting
    return String();
  }

  // Convert the formatted buffer to a String
  return String(buffer);
}


