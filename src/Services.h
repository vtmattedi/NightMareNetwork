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

};
AcController Ac;
extern AcController Ac;

struct LightController
{
    ServerVariable<bool> LightState;
    ServerVariable<uint32_t> AutomationRestore;
    ServerVariable<int> Hue;
    void sync()
    {
        LightState.sync();
        AutomationRestore.sync();
        Hue.sync();
    }
};
LightController BedLights;
extern LightController BedLights;
