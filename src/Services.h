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
    AcController() : Temperature((char *)"Ac Temp"), Hsleep((char *)"Ac Hw Sleep"), Ssleep((char *)"Ac Sw Sleep"), SetTemp((char *)"Ac Set Temp")
    {
    }
    void on_any_value_changed(void (*new_on_value_changed)())
    {
        Temperature.on_value_changed = new_on_value_changed;
        Hsleep.on_value_changed = new_on_value_changed;
        Ssleep.on_value_changed = new_on_value_changed;
        SetTemp.on_value_changed = new_on_value_changed;
    }
};
AcController AcControl;
extern AcController AcControl;

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
    LightController() : LightState((char *)"Light State"), AutomationRestore((char *)"Light Automation Restore"),
                        HexColor((char *)"Light Hex Code"), Brightness((char *)"Light Brightness")
    {
    }
};
LightController BedLights;
extern LightController BedLights;
