#include <Services/LightController.h>
#ifdef COMPILE_LIGHTCONTROLLER
/// @brief Assigned to `ServerVariables<>` to be called and then formats and calls `FormatSend`
/// @param id The `userid` of the `ServerVariables<>`
/// @param value The `value` of the `ServerVariables<>`
/// @param hostname The hostname stored in the `userinfo` of the `ServerVariables<>`
void static LightSendById(uint8_t id, String value, String hostname)
{
    String senddata = "";
    if (id == LIGHTSTATE_ID)
    {
        // senddata = "u";
        senddata += value;
        FormatSend("/Lights", senddata, hostname);
    }
};

/// @brief Constructor that sets up the ServerVariables.
/// @param Hostname The hostname of the host device.
LightController::LightController(const char *Hostname)
{
    // Set up the server hostname of the controller device
    MQTTHostName = Hostname;
    LightState.userinfo = Hostname;
    AutomationRestore.userinfo = Hostname;
    // Set up the ServerVariables
    LightState.label = (char *)"Light State";
    AutomationRestore.label = (char *)"Light Automation Restore";
    LightState.userid = LIGHTSTATE_ID;
    AutomationRestore.userid = RESTORE_ID;
    // Assign the function to be called when we change some ServerVariable on the client side.
    LightState.on_send_with_info = LightSendById;
    AutomationRestore.on_send_with_info = LightSendById;
}

/// @brief Assign a function to all ServerVariables on_changed status.
/// @param callback Function to be called when the status of the ac controller changes
void LightController::On_any_value_changed(void (*callback)(void))
{
    LightState.on_value_changed = callback;
    AutomationRestore.on_value_changed = callback;
}

/// @brief Shutdown the lights.
void LightController::Shutdown()
{
    LightState.force(false);
    // FormatSend("/Lights", "0", MQTTHostName);
}

/// @brief Toggles the current state of the light
void LightController::ToggleLight()
{
    SetLight(!LightState.value);
}

/// @brief Sets the light on/off.
/// @param state the desired state true = on.
void LightController::SetLight(bool state)
{
    LightState.change(state);

}

/// @brief Toggles the light to the opposite state and stops the automations.
void LightController::ToggleForce()
{
    LightState.force(!LightState.value);
    AutomationRestore.force(now() + 2 * HOUR);
    FormatSend("/Lights", "toggle-force", MQTTHostName);
}

/// @brief Whether or not the server values are stale.
/// @return True if server values are stale
bool LightController::IsStale()
{
    return LightState.stale;
}

/// @brief Restore the device to automatic mode.
void LightController::RestoreAutomations()
{
    FormatSend("/Lights", "auto", MQTTHostName);
    AutomationRestore.change(0);
}

/// @brief Syncs all the ServerVariables
void LightController::Sync()
{
    LightState.sync();
    AutomationRestore.sync();
}

/// @brief Parses the server state of the Ac Controller.
/// @param Json the String in Json format sent by the server.
void LightController::ParseServerState(String Json)
{
    DynamicJsonDocument DocJson(512);
    deserializeJson(DocJson, Json);
    AutomationRestore.handleServer(DocJson["Automation"]);
    LightState.handleServer(DocJson["State"]);
}

/// @brief Gets the current state of the light.
/// @return True if the light is on, false otherwise.
bool LightController::CurrentState()
{
    return LightState.value;
}

void LightController::SoftwareReset()
{
    FormatSend("/console/in", "reset", MQTTHostName);
}

#endif