#include <NightMareNetwork.h>

void setup()
{
    PersistentSettings.begin();
    PersistentSettings.setFlag("smoke", true);
    SystemState.setFlag("smoke", PersistentSettings.getFlag("smoke"));
    Telemetry.start();
}

void loop() {}
