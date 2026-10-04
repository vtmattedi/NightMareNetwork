#pragma once

#include <cstdint>

namespace NightMare
{
enum class ConnectivityState : uint8_t
{
    STOPPED,
    STARTING,
    READY,
    CONNECTING,
    CONNECTED,
    SUSPENDED,
    ERROR
};

enum class ConnectivitySuspendReason : uint8_t
{
    WIFI_SCAN
};

struct ConnectivityStatus
{
    bool supported = false;
    bool enabled = false;
    ConnectivityState state = ConnectivityState::STOPPED;
};

inline const char *ConnectivityStateName(ConnectivityState state)
{
    switch (state)
    {
    case ConnectivityState::STOPPED: return "STOPPED";
    case ConnectivityState::STARTING: return "STARTING";
    case ConnectivityState::READY: return "READY";
    case ConnectivityState::CONNECTING: return "CONNECTING";
    case ConnectivityState::CONNECTED: return "CONNECTED";
    case ConnectivityState::SUSPENDED: return "SUSPENDED";
    case ConnectivityState::ERROR: return "ERROR";
    }
    return "ERROR";
}
}
