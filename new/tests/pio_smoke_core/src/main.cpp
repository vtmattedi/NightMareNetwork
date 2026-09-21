#include <NightMare.h>

void setup()
{
    gDeviceIdentity.begin();
    SystemState.setFlag("booted", true);
    SystemState.setFlag("clock_valid", NightMare::Time::valid());
}

void loop() {}
