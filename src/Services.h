#include <Arduino.h>
#include <NightMareNetwork.h>

struct AcController
{
    ServerVariable<int> Temperature;
    ServerVariable<uint32_t> Hsleep;
    ServerVariable<uint32_t> Ssleep;
    ServerVariable<double> SetTemp;

    void sync()
    {
        Temperature.sync();
        Hsleep.sync();
        Ssleep.sync();
        SetTemp.sync();
    }
    AcController() : Temperature("Ac Temp"), Hsleep("Ac Hw Sleep"), Ssleep("Ac Sw Sleep"), SetTemp("Ac Set Temp")
    {

    }

};
AcController Ac;
extern AcController Ac;

struct LightController
{
    ServerVariable<bool> LightState;
    ServerVariable<uint32_t> AutomationRestore;
    ServerVariable<uint32_t> HexColor;
    ServerVariable<int> Brightness;
    void sync()
    {
        LightState.sync();
        AutomationRestore.sync();
        HexColor.sync();
        Brightness.sync();
    }
    LightController(): LightState ("Light State"), AutomationRestore("Light Automation Restore"), 
    HexColor("Light Hex Code"),Brightness("Light Brightness")
    {

    }
};
LightController BedLights;
extern LightController BedLights;
