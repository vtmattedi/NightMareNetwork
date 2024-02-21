/*----------------------------------------------------------*/
///
///@file LightController.h -
/// Implements a class to control a light with muiltcolor options,
/// or a light stripon the NightMare Network
/// Author: Vitor Mattedi Carvalho
/// Date: 21-02-2024
/// Version: 1.1
///         Created.
/*----------------------------------------------------------*/

#pragma once
#include <Services/ServicesCore.h>
#include <Services/LightController.h>
#include <Core/ServerVariables.h>

class LightColorController : public LightController
{
private:
    const char *MQTTHostName = (char *)"";

public:
    ServerVariable<uint32_t> HexColor;
    ServerVariable<int> Brightness;
    LightColorController(const char *);
    void SetColor(int);
    void SetBrightness(uint8_t);
    void Setinterval(int);
    void SetMode(String);
    void Shutdown();
    void Sync();
    void On_any_value_changed(void (*callback)(void));
};
