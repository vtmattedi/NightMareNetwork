#pragma once

#include <NightMare/HardwareProfile.h>
#include <board.h>

namespace NMHardware
{
inline Profile projectProfile()
{
    static const Connection connections[] = {
        {
            "DS18B20 data",
            PIN_ONE_WIRE,
            Direction::Bidirectional,
            Pull::External,
            false,
            "1-Wire data; external 4.7 kOhm pull-up to 3.3 V"
        },
    };

    return {
        BOARD_NAME,
        connections,
        sizeof(connections) / sizeof(connections[0])
    };
}
}
