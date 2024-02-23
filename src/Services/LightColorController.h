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
/// @brief The HostName of the Mqtt Service
    const char *MQTTHostName = (char *)"";

public:
    /// @brief The color of the light or strip.
    ServerVariable<uint32_t> HexColor;
    /// @brief the birghtness of the light.
    ServerVariable<int> Brightness;
    LightColorController(const char *);
    void SetColor(int);
    void SetBrightness(uint8_t);
    void Setinterval(uint16_t);
    void ParseServerState(String);
    void SetMode(String);
    void Sync();
    void On_any_value_changed(void (*callback)(void));
};
