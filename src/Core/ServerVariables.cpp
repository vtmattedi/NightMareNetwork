#include <Core/ServerVariables.h>

/// @brief Constructor with initial value.
/// @tparam T The type of the variable.
/// @param initialValue The initial value of the sensor.
template <typename T>
ServerVariable<T>::ServerVariable(T initialValue)
{
    value = initialValue;
}
/// @brief A default constructor.
/// @tparam T The type of the variable.
template <typename T>
ServerVariable<T>::ServerVariable()
{
}
/// @brief A constructor with a defined label.
/// @tparam T The type of the variable.
/// @param _label The label for this variable.
template <typename T>
ServerVariable<T>::ServerVariable(const char *_label)
{
    label = _label;
}
/// @brief Handles a value coming from the server with the value of this variable there
/// @tparam T The type of the variable
/// @param newServerValue Value received from the server
template <typename T>
void ServerVariable<T>::handleServer(T newServerValue)
{
    serverValue = newServerValue;
    stale = false;
    millis_last_server_received = millis();
    if (debug)
    {
        Serial.printf("[%d][%s]...new Server value: [%s] client value is: [%s], assert is: %s...\n",millis(), label, String(newServerValue).c_str(), String(value).c_str(), assert_handled ? "Handled" : "Waiting");
    }

    if (assert_handled)
    {
        if (value != newServerValue)
        {
            value = newServerValue;
            onValueChanged();
        }
    }
}
/// @brief Sync the value between server and client. Periodically asserts the client value,
/// flag as stale if applicable and also calls `sendChanges()` if `millis_to_delay_before_send` > 0
/// and needs to be handled. Should be called periodically for the proper usage.
/// @tparam T The type of the variable
template <typename T>
void ServerVariable<T>::sync()
{
    if (millis() - millis_last_server_received > millis_to_stale && !stale)
    {
        stale = true;
        onValueChanged();
        if (debug)
            Serial.printf("[%d][%s]...Variable Stale...\n", millis(), label);
    }
    if (millis() > millis_to_assert_server && !assert_handled)
    {
        bool equal = value == serverValue;
        if (debug)
            Serial.printf("[%d][%s]...Server Variable asserted server is: [%s] client is :[%s]...\n", millis(), label, String(serverValue).c_str(), String(value).c_str());
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

    if (millis() > millis_to_send && !send_handled)
    {
        sendChanges();
    }
}
/// @brief Calls `on_send` AND `on_send_with_info` if they are not NULL. Sets `send_handled` to true
/// @tparam T The type of the variable
template <typename T>
void ServerVariable<T>::sendChanges()
{
    if (on_send)
    {
        (*on_send)(value);
        if (debug)
            Serial.printf("[%d][SendValueTriggerd][%s] delay ended\n",millis(), label, millis());
    }
    if (on_send_with_info)
    {
        (*on_send_with_info)(userid, String(value), String(userinfo));
        if (debug)
            Serial.printf("[%d][%s] [SendByIdTriggerd] [id:%d] delay ended\n", millis(), label,  userid);
    }
    send_handled = true;
}
/// @brief Change the value on the client side, sets the flag to send the new value and also
/// sets the flag to assert the value and sets the timestamp for the assert.
/// @tparam T The type of the variable
/// @param newValue The new value of the client
template <typename T>
void ServerVariable<T>::change(T newValue)
{
    bool value_changed = false;
    // if (debug)
    //     Serial.printf("[%s], [%s], [%d]\n", String(value).c_str(), String(newValue).c_str(), newValue != value);
    if (value != newValue)
    {
        value_changed = true;
        value = newValue;
    }
    if (value_changed)
    {
        if (millis_to_delay_before_send > 0)
        {
            millis_to_send = millis() + millis_to_delay_before_send;
            if (debug)
                Serial.printf("[%d][%s] delay Started\n", millis(), label);
        }
        else
            sendChanges();
        send_handled = false;
    }

    millis_to_assert_server = millis() + ASSERT_DELAY;
    assert_handled = false;
    if (debug)
    if (value_changed)
        onValueChanged();
}
/// @brief Forces an state on the variable. Does not trigger `on_send`.
/// @tparam T The type of the variable
/// @param newValue
template <typename T>
void ServerVariable<T>::force(T newValue)
{
    if (value != newValue)
    {
        value = newValue;
        onValueChanged();
    }
    if (debug)
        Serial.printf("[%d][%s]...[Forced]new value: [%s]...\n",millis(), label, String(value).c_str());
    assert_handled = false;
    millis_to_assert_server = millis() + 4 * ASSERT_DELAY;
}
/// @brief Calls `on_value_changed` if it is not NULL.
/// @tparam T The type of the variable
template <typename T>
void ServerVariable<T>::onValueChanged()
{
    if (on_value_changed)
    {
        (*on_value_changed)();
    }
}

/************************************************
 * CPP COMPILE OPTIONS                          *
 ************************************************
 */
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
template class ServerVariable<uint8_t>;
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
