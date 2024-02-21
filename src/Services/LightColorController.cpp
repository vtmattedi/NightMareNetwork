#include <Services/LightColorController.h>

/// @brief Assigned to `ServerVariables<>` to be called and then formats and calls `FormatSend`
/// @param id The `userid` of the `ServerVariables<>`
/// @param value The `value` of the `ServerVariables<>`
/// @param hostname The hostname stored in the `userinfo` of the `ServerVariables<>`
void static LightColorSendById(uint8_t id, String value, String hostname)
{

    String senddata = "";

    if (id == HEXCOLOR_ID)
    {
        senddata = "";
        senddata += value;
        FormatSend("/Color", senddata, hostname);
    }
    else if (id == LIGHTSTATE_ID)
    {
        senddata = "u";
        senddata += value;
        FormatSend("/Lights", senddata, hostname);
    }
    else if (id == BRIGHTNESS_ID)
    {
        senddata = "DOOR ";
        senddata += value;
        FormatSend("/console/in", senddata, hostname);
    }
}

LightColorController::LightColorController(const char *Hostname) : LightController()
{
    // Sets the hostname.
    MQTTHostName = Hostname;
    Brightness.userinfo = Hostname;
    LightState.userinfo = Hostname;
    HexColor.userinfo = Hostname;
    AutomationRestore.userinfo = Hostname;
    // initialized the labels for debug if needed
    HexColor.label = (char *)"Light Hex Code";
    Brightness.label = (char *)"Light Brightness";
    LightState.label = (char *)"Light State";
    AutomationRestore.label = (char *)"Light Automation Restore";
    // Set up the ID of each SeverVariable os we can identify which format to use when sending a value
    HexColor.userid = HEXCOLOR_ID;
    Brightness.userid = BRIGHTNESS_ID;
    LightState.userid = LIGHTSTATE_ID;
    AutomationRestore.userid = RESTORE_ID;
    // Assign the function to be called when we change some ServerVariable on the client side.
    Brightness.on_send_with_info = LightColorSendById;
    LightState.on_send_with_info = LightColorSendById;
    HexColor.on_send_with_info = LightColorSendById;
    AutomationRestore.on_send_with_info = LightColorSendById;
}

void LightColorController::On_any_value_changed(void (*callback)(void))
{
    LightState.on_value_changed = callback;
    AutomationRestore.on_value_changed = callback;
    HexColor.on_value_changed = callback;
    Brightness.on_value_changed = callback;
}

void LightColorController::Shutdown()
{
    LightState.force(false);
    FormatSend("/Lights", "u0", MQTTHostName);
}

void LightColorController::SetBrightness(uint8_t brightness)
{
    Brightness.change(brightness);
}

void LightColorController::SetColor(int hexcode)
{
    if (hexcode > 0x00ffffff)
    {
        Serial.printf("Invalid Hexcode!\n");
        return;
    }
    HexColor.change(hexcode);
}

void LightColorController::Setinterval(int speed)
{
    FormatSend("/setinterval", String(speed), MQTTHostName);
}
void LightColorController::SetMode(String Mode)
{
    FormatSend("/Mode", Mode, MQTTHostName);
}

void LightColorController::Sync()
{
    LightState.sync();
    AutomationRestore.sync();
    HexColor.sync();
    Brightness.sync();
}