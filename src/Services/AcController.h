/*----------------------------------------------------------*/
///
///@file LightController.h -
/// Implements a class to control an Ac/room Temperature 
/// controller on the NightMare Network
/// Author: Vitor Mattedi Carvalho
/// Date: 21-02-2024
/// Version: 1.1
///         Created.
/*----------------------------------------------------------*/

#pragma once
#include <Services/ServicesCore.h>
#include <core/ServerVariables.h>

class AcController
{
private:
    /// @brief The HostName of the Mqtt Service
    const char *MQTTHostName = (char *)"";

public:
    /// @brief The current Temperature of the Ac. Negative means it is off.
    ServerVariable<int> AcTemperature;
    /// @brief Time left for the Ac to turn off (Own timer).
    ServerVariable<uint32_t> HwSleep;
    /// @brief Time Left for the Ac to turn off (by the Controller).
    ServerVariable<uint32_t> SWSleep;
    /// @brief The room temperature target. Negative means off.
    ServerVariable<double> AcTarget;
    /// @brief Timestamp of when door was open, 0 means closed.
    ServerVariable<uint32_t> doorOpen;
    /// @brief Wheater or not the Sleep In will be triggered tonight.
    ServerVariable<bool> sleepIn;

public:
    AcController(const char *);
    bool AcTargetEnabled();
    double CurrentTarget();
    bool IsStale();
    void ManualSync(byte AcTemp, bool PowerOn);
    void On_any_value_changed(void (*)(void));
    void ParseServerState(String);
    void SetAcTarget(double target);
    void SetAcTarget(bool enable);
    void SetAcTargetDelta(double delta);
    void SetAcTemperature(byte Temperature);
    void SetAcTemperatureDelta(int8_t delta);
    void SetAcPower(bool power);
    void SetSleepIn(bool sleep);
    void Shutdown();
    void Sync();
    void ToggleAcTarget();
    void ToggleAcPower();

private:
};
