#include "SystemState.h"

namespace
{
struct BitLocation
{
    size_t word;
    uint32_t mask;
};

constexpr BitLocation locate(uint16_t index)
{
    return {
        index >> 5,
        uint32_t{1} << (index & 31)
    };
}
}

SystemStateStore SystemState;

bool SystemStateStore::get(SystemFlag flag) const
{
    const uint16_t index = static_cast<uint16_t>(flag);
    if (index >= SystemFlagCount)
        return false;
    const BitLocation bit = locate(index);
    portENTER_CRITICAL(&mux_);
    const bool value = (states_[bit.word] & bit.mask) != 0;
    portEXIT_CRITICAL(&mux_);
    return value;
}

void SystemStateStore::set(SystemFlag flag)
{
    const uint16_t index = static_cast<uint16_t>(flag);
    if (index >= SystemFlagCount)
        return;
    const BitLocation bit = locate(index);
    portENTER_CRITICAL(&mux_);
    states_[bit.word] |= bit.mask;
    portEXIT_CRITICAL(&mux_);
}

void SystemStateStore::clear(SystemFlag flag)
{
    const uint16_t index = static_cast<uint16_t>(flag);
    if (index >= SystemFlagCount)
        return;
    const BitLocation bit = locate(index);
    portENTER_CRITICAL(&mux_);
    states_[bit.word] &= ~bit.mask;
    portEXIT_CRITICAL(&mux_);
}

void SystemStateStore::request(SystemRequest request)
{
    const uint16_t index = static_cast<uint16_t>(request);
    if (index >= SystemRequestCount)
        return;
    const BitLocation bit = locate(index);
    portENTER_CRITICAL(&mux_);
    requests_[bit.word] |= bit.mask;
    portEXIT_CRITICAL(&mux_);
}

bool SystemStateStore::pending(SystemRequest request) const
{
    const uint16_t index = static_cast<uint16_t>(request);
    if (index >= SystemRequestCount)
        return false;
    const BitLocation bit = locate(index);
    portENTER_CRITICAL(&mux_);
    const bool value = (requests_[bit.word] & bit.mask) != 0;
    portEXIT_CRITICAL(&mux_);
    return value;
}

bool SystemStateStore::take(SystemRequest request)
{
    const uint16_t index = static_cast<uint16_t>(request);
    if (index >= SystemRequestCount)
        return false;
    const BitLocation bit = locate(index);
    portENTER_CRITICAL(&mux_);
    const bool value = (requests_[bit.word] & bit.mask) != 0;
    requests_[bit.word] &= ~bit.mask;
    portEXIT_CRITICAL(&mux_);
    return value;
}
