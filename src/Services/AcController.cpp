#include <Services/AcController.h>

/// @brief Assigned to `ServerVariables<>` to be called and then formats and calls `FormatSend`
/// @param id The `userid` of the `ServerVariables<>`
/// @param value The `value` of the `ServerVariables<>`
/// @param hostname The hostname stored in the `userinfo` of the `ServerVariables<>`
void static AcControllerSendById(uint8_t id, String value, String hostname)
{
    String senddata = "";

    if (id == ACTEMP_ID)
    {
        senddata = "SETTEMP ";
        senddata += value;
        FormatSend("/console/in", senddata, hostname);
    }
    if (id == ACTARGET_ID)
    {
        senddata = "TARGET ";
        senddata += value;
        FormatSend("/console/in", senddata, hostname);
    }
    else if (id == SLEEPIN_ID)
    {
        senddata = "SLEEP-IN ";
        senddata += value;
        FormatSend("/console/in", senddata, hostname);
    }
    else if (id == DOOR_ID)
    {
        senddata = "DOOR ";
        senddata += value;
        FormatSend("/console/in", senddata, hostname);
    }
}

/// @brief Syncs all ServerVariables. should be called frequently.
void AcController::Sync()
{
    AcTemperature.sync();
    HwSleep.sync();
    SWSleep.sync();
    AcTarget.sync();
    doorOpen.sync();
    sleepIn.sync();
}

/// @brief AcController
/// @param Hostname The name of the device running the Ac controlling in the network (It should be able to read from the same mqtt/tcp network as the one we are publishing)
AcController::AcController(const char *Hostname)
{
    // Sets the hostname.
    MQTTHostName = Hostname;
    AcTemperature.userinfo = Hostname;
    HwSleep.userinfo = Hostname;
    SWSleep.userinfo = Hostname;
    AcTarget.userinfo = Hostname;
    sleepIn.userinfo = Hostname;
    // initialized the labels for debug if needed
    AcTemperature.label = (char *)"Ac Temp";
    HwSleep.label = (char *)"Ac Hw Sleep";
    SWSleep.label = (char *)"Ac Sw Sleep";
    AcTarget.label = (char *)"Ac Set Temp";
    sleepIn.label = (char *)"Ac Sleep In";
    // Set up the ID of each SeverVariable os we can identify which format to use when sending a value
    AcTemperature.userid = ACTEMP_ID;
    AcTarget.millis_to_delay_before_send = 500; // Added a delay for the AcTarget
    AcTarget.userid = ACTARGET_ID;
    SWSleep.userid = SWSLEEP_ID;
    HwSleep.userid = HWSLEEP_ID;
    sleepIn.userid = SLEEPIN_ID;
    // Assign the function to be called when we change some ServerVariable on the client side.
    AcTemperature.on_send_with_info = AcControllerSendById;
    HwSleep.on_send_with_info = AcControllerSendById;
    SWSleep.on_send_with_info = AcControllerSendById;
    AcTarget.on_send_with_info = AcControllerSendById;
    sleepIn.on_send_with_info = AcControllerSendById;
}

/// @brief Wheather or not our info about the server is stale.
/// @return The stale status of the most imporant ServerVariable, which can be assumed to be the current stale status.
bool AcController::IsStale()
{
    // if(complete)
    // return AcTarget.stale || AcTemperature.stale ||  HwSleep.stale || SWSleep.stale || doorOpen.stale || sleepIn .stale;
    // else
    return AcTarget.stale;
}

/// @brief Assign a function to all ServerVariables on_changed status.
/// @param callback Function to be called when the status of the ac controller changes
void AcController::On_any_value_changed(void (*callback)(void))
{
    AcTemperature.on_value_changed = callback;
    HwSleep.on_value_changed = callback;
    SWSleep.on_value_changed = callback;
    AcTarget.on_value_changed = callback;
    sleepIn.on_value_changed = callback;
    AcTarget.on_value_changed = callback;
    DoorState.on_value_changed = callback;
}

/// @brief Gets the current target of the Ac Controller
/// @return The current room temperature target of the Ac Controller. A negative number means it is not controlling.
double AcController::CurrentTarget()
{
    return AcTarget.value;
}

/// @brief Sets the Ac Temperature
/// @param Temperature The temperature desired
void AcController::SetAcTemperature(byte Temperature)
{
    if (Temperature < 18 || Temperature > 30)
    {
        Serial.println("{AcController}...Skynet cannot handle AC outside [18,30]");
        return;
    }
    FormatSend("/console/in", String("SETTEMP ") + String(Temperature), MQTTHostName);
    AcTemperature.change(Temperature);
}

/// @brief Sets the Ac temperature based on the difference of the current temperature. I.E. delta = 1 will increase the Ac temperature in 1 degree.
/// @param delta The difference in temperature desired.
void AcController::SetAcTemperatureDelta(int8_t delta)
{
    byte Temperature = AcTemperature.value + delta;
    if (Temperature < 18 || Temperature > 30)
    {
        Serial.println("{AcController}...Skynet cannot handle AC outside [18,30]");
        return;
    }
    AcTemperature.change(Temperature);
}

/// @brief Sets the Ac on or Off.
/// @param power On/Off true = On.
void AcController::SetAcPower(bool power)
{
    AcTemperature.force(power ? _abs(AcTemperature.value) : -1 * _abs(AcTemperature.value));
    FormatSend("/console/in", String("POWER ") + String(power ? "1" : "0"), MQTTHostName);
}

/// @brief Toggles the Ac on or off
void AcController::ToggleAcPower()
{
    SetAcPower(!(AcTemperature.value > 0));
}

/// @brief Wheather or not the Ac Controller is active controlling the room temperature.
/// @return True if the Ac Controller is current temeprature target
bool AcController::AcTargetEnabled()
{
    return CurrentTarget() > 0;
}

/// @brief Toggles the Control of the room temperature on or off.
void AcController::ToggleAcTarget()
{
    SetAcTarget(!AcTargetEnabled());
}

/// @brief Sets the Control of the room temperature on or off.
/// @param enable On/Off true = On
void AcController::SetAcTarget(bool enable)
{
    SetAcTarget(enable ? _abs(CurrentTarget()) : _abs(CurrentTarget()) * -1);
}

/// @brief Sets the Control of the room temperature on and to the the speficified temperature.
/// @param target The target room temperature.
void AcController::SetAcTarget(double target)
{
    AcTarget.change(target);
}

/// @brief Sets the control of the room temperature on and to a delta from the current temeprature.
/// @param delta The difference in temperature desired.
void AcController::SetAcTargetDelta(double delta)
{
    double newTarget = _abs(CurrentTarget()) + delta;
    Serial.printf("nt = %f\n", newTarget);
    AcTarget.change(newTarget);
}

/// @brief Parses the server state of the Ac Controller.
/// @param Json the String in Json format sent by the server.
void AcController::ParseServerState(String Json)
{
    DynamicJsonDocument DocJson(512);
    deserializeJson(DocJson, Json);

    AcState = DocJson["AcState"];
    DoorState.change(DocJson["DoorState"]);
    AcTemperature.handleServer(DocJson["Temp"]);
    HwSleep.handleServer(DocJson["Hsleep"]);
    SWSleep.handleServer(DocJson["Ssleep"]);
    AcTarget.handleServer(DocJson["Settemp"]);
    sleepIn.handleServer(DocJson["SleepIn"]);
    doorOpen.handleServer(DocJson["Door"]);
    sensorValue = DocJson["CurrTemp"].as<float>();
}

/// @brief Disables the room temperature control and shutdown the Ac
void AcController::Shutdown()
{
    if (AcTemperature.value > 0)
        AcTemperature.force(AcTemperature.value * -1);
    if (AcTarget.value > 0)
        AcTarget.force(AcTarget.value * -1);
    FormatSend("/console/in", "TARGET -1", MQTTHostName);
}

/// @brief Manually Syncs the Ac Control and the Hardware Unit
/// @param AcTemp The Temperature of the Ac Unit.
/// @param PowerOn Whether or not te Ac Unit is On.
void AcController::ManualSync(byte AcTemp, bool PowerOn)
{
    String manualsync = "manualsync ";
    manualsync += PowerOn;
    manualsync += " ";
    manualsync += AcTemp;
    FormatSend("/console/in", manualsync, MQTTHostName);
    AcTemperature.force(PowerOn ? AcTemp : -1 * (int)AcTemp);
}

/// @brief Sets the SleepIn status for the night.
/// @param sleep Turn on/of SleepIn for tonight.
void AcController::SetSleepIn(bool sleep)
{
    sleepIn.change(sleep);
}

/// @brief Sends an raw IR code.
/// @param command The IR code's name.
void AcController::SendRawCommand(String command)
{
    String sendstr = "SENDIR ";
    sendstr += command;
    FormatSend("/console/in", sendstr, MQTTHostName);
}

/// @brief Gets the time when Ac will shutdown via user request (SW) or via the
/// unit's own command (Hw)
/// @return 0 if no sleep is set, Sw sleep if it is set or Hw sleep if it is set and Sw is not
uint32_t AcController::GetSleepTime()
{
    if (SWSleep.value > 0)
        return SWSleep.value;
    else
        return HwSleep.value;
}

/// @brief If AcTarget is set, stops; Toggle Ac State.
void AcController::Toggle()
{
    if (AcTargetEnabled())
        ToggleAcTarget();

    ToggleAcPower();
}


int8_t AcController::GetState()
{
    return AcState;
}

void AcController::SetDoorPause(bool pause)
{
    Send_to_MQTT(String(MQTTHostName) + "/console/in", "PAUSEDOORSENSOR " + String((int)pause));
    DoorState.force(bitWrite(DoorState.value, 1, pause));
}

bool AcController::GetDoorPause()
{
    return bitRead(DoorState.value, 1);
}

bool AcController::GetDoorOpen()
{
    return bitRead(DoorState.value, 0);
}

bool AcController::GetAcPausedByDoorSensor()
{
     //Serial.printf("ACPD [%d] %d, %d, %d\n",DoorState.value, bitRead(doorOpen.value, 2), bitRead(doorOpen.value, 1), bitRead(doorOpen.value, 2) && !bitRead(doorOpen.value, 1));
    return (bitRead(DoorState.value, 2) && !bitRead(DoorState.value, 1));
}

void AcController::SoftwareReset()
{
    FormatSend("/console/in", "reset", MQTTHostName);
}