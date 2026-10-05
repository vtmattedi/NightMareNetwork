#pragma once

#include <Network/NmConnection.h>

namespace NightMare
{
// Pure routing decision used by the coordinator and smoke regression tests.
// espNowEligible includes probable-target and required-uplink policy. Usable
// means the transport has actually connected; a probable target alone never
// causes the active route to change.
inline ConnectionType ResolveNetworkRoute(ConnectionType preferred,
                                          ConnectionType current,
                                          bool mqttUsable,
                                          bool espNowUsable,
                                          bool espNowEligible)
{
    const auto usable = [mqttUsable, espNowUsable](ConnectionType connection) {
        return connection == ConnectionType::MQTT ? mqttUsable :
               connection == ConnectionType::ESP_NOW ? espNowUsable : false;
    };
    const auto eligible = [&usable, espNowEligible](ConnectionType connection) {
        return usable(connection) &&
               (connection != ConnectionType::ESP_NOW || espNowEligible);
    };

    if (preferred != ConnectionType::AUTO && eligible(preferred))
        return preferred;
    if (usable(current))
        return current;

    constexpr ConnectionType fallback[] = {ConnectionType::ESP_NOW,
                                            ConnectionType::MQTT};
    if (preferred != ConnectionType::AUTO && eligible(preferred))
        return preferred;
    for (ConnectionType connection : fallback)
        if (connection != preferred && eligible(connection))
            return connection;
    return ConnectionType::AUTO;
}
}
