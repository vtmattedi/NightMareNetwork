#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>

enum class SystemFlag : uint16_t
{
    OtaRunning = 0,
    TimeSynced,
    PersistentStorageReady,

    Count
};

enum class SystemRequest : uint16_t
{
    PublishStatus = 0,
    PublishManifest,
    PublishConsumeManifest,
    PublishResourceStates,
    PublishInfo,
    PublishHardwareJson,
    PublishHardwareMsgPack,

    Count
};

inline constexpr size_t SystemFlagCount = static_cast<size_t>(SystemFlag::Count);
inline constexpr size_t SystemRequestCount = static_cast<size_t>(SystemRequest::Count);
inline constexpr size_t SystemFlagWordCount = (SystemFlagCount + 31) / 32;
inline constexpr size_t SystemRequestWordCount = (SystemRequestCount + 31) / 32;

/// Allocation-free runtime facts and deferred framework publication requests.
/// The API is task-safe. It is not safe to call from an ISR.
class SystemStateStore
{
public:
    bool get(SystemFlag flag) const;
    void set(SystemFlag flag);
    void clear(SystemFlag flag);

    void request(SystemRequest request);
    bool pending(SystemRequest request) const;
    bool take(SystemRequest request);

private:
    uint32_t states_[SystemFlagWordCount]{};
    uint32_t requests_[SystemRequestWordCount]{};
    mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};

extern SystemStateStore SystemState;
