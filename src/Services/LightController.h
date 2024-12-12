/*----------------------------------------------------------*/
///
///@file LightController.h -
/// Implements a class to control a light on the NightMare Network
/// Author: Vitor Mattedi Carvalho
/// Date: 21-02-2024
/// Version: 1.1
///         Created.
/*----------------------------------------------------------*/

#pragma once
#include <Services/ServicesCore.h>

class LightController
{
protected:
    /// @brief The HostName of the Mqtt Service
    const char *MQTTHostName = (char *)"";

public:
    /// @brief The state (on/off) of the light.
    ServerVariable<bool> LightState;
    /// @brief The timestamp of when the automations will automatically be restored.
    //  0 if automations are enabled.
    ServerVariable<uint32_t> AutomationRestore;

public:
    LightController(const char *);
    LightController();
    bool CurrentState();
    bool IsStale();
    void On_any_value_changed(void (*)(void));
    void ParseServerState(String);
    void RestoreAutomations();
    void SetLight(bool);
    void Shutdown();
    void Sync();
    void ToggleLight();
    void ToggleForce(); 
};
