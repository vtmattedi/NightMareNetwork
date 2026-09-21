#pragma once

#include <Arduino.h>

namespace NMHardware
{
enum class Direction : uint8_t { Input, Output, Bidirectional, Power, Ground, Bus };
enum class Pull : uint8_t { None, Up, Down, External };

struct Connection
{
    const char *name;
    int16_t pin;
    Direction direction;
    Pull pull;
    bool activeLow;
    const char *note;
};

struct Profile
{
    const char *boardName;
    const Connection *connections;
    size_t connectionCount;
};

// If the project has no NightMareHardware.h, this returns an unnamed board
// with no advertised physical connections.
Profile getProfile();
}
