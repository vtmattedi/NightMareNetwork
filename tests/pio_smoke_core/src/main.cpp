#include <NightMare.h>

RuntimeState smokeState;
Config<uint32_t> smokeConfig("smoke_config", 7);

void setup()
{
    gDeviceIdentity.begin();
    smokeState.setFlag("booted", true);
    smokeState.setFlag("clock_valid", NightMare::Time::valid());

    const size_t staticCount = configManager().count();
    const bool staticAutoBound = staticCount == 1 &&
                                 configManager().handle("get smoke_config") == "7" &&
                                 !configManager().bind(&smokeConfig);
    bool scopedLifecycle = false;
    {
        Config<int> scoped("scoped_config", 11);
        const bool autoBound = configManager().count() == staticCount + 1 &&
                               configManager().handle("get scoped_config") == "11";
        const bool manuallyUnbound = configManager().unbind(&scoped) &&
                                     configManager().handle("get scoped_config") ==
                                         "ERROR: unknown config";
        const bool manuallyRebound = configManager().bind(&scoped) &&
                                     !configManager().bind(&scoped);
        scopedLifecycle = autoBound && manuallyUnbound && manuallyRebound;
    }
    const bool scopedAutoUnbound = configManager().count() == staticCount &&
                                   configManager().handle("get scoped_config") ==
                                       "ERROR: unknown config";
    Config<int> invalidName("invalid name", 13);
    const bool failedAutoBindStaysLocal = invalidName.value() == 13 &&
                                         configManager().count() == staticCount;
    smokeState.setFlag("config_lifecycle", staticAutoBound && scopedLifecycle &&
                                               scopedAutoUnbound && failedAutoBindStaysLocal);
}

void loop() {}
