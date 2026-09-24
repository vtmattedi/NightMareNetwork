#include <NightMare.h>

RuntimeState smokeState;

void setup()
{
    gDeviceIdentity.begin();
    smokeState.setFlag("booted", true);
    smokeState.setFlag("clock_valid", NightMare::Time::valid());
}

void loop() {}
