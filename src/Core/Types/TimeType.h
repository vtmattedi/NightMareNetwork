#pragma once

#include <Arduino.h>
#include <time.h>
/// @brief A valid local time of day. It has no date, timezone, epoch, or
/// duration semantics.
class TimeType
{
public:
    TimeType() = default;
    /// Out-of-range components produce the deterministic default 00:00:00.
    TimeType(uint8_t hour, uint8_t minute, uint8_t second = 0);

    uint8_t hour() const { return hours_; }
    uint8_t minute() const { return minutes_; }
    uint8_t second() const { return seconds_; }

    String toString() const;

    bool operator==(const TimeType &other) const;
    bool operator!=(const TimeType &other) const { return !(*this == other); }
    /// @brief Returns true if the time matches the given epoch's local time, optionally including seconds.
    /// @example `TimeType(14, 30).matches(1680000000)` returns true if the local time of the epoch (1680000000) is 14:30.
    /// @example `TimeType(14, 30).matches()` returns true if the local time of the epoch is 14:30.
    /// @param epoch The epoch time to compare against. Defaults to the current time.
    /// @param include_seconds Whether to include seconds in the comparison. Defaults to false.
    /// @return true if the time matches the given epoch's local time, false otherwise.
    bool matches(const time_t &epoch = time(nullptr), bool include_seconds = false) const;

private:
    uint8_t hours_ = 0;
    uint8_t minutes_ = 0;
    uint8_t seconds_ = 0;
};

/// Canonical `HH:MM:SS` representation.
String encodeTime(const TimeType &time);

/// Accepts HH:MM:SS, HH:MM, HHMMSS, HHMM, H:M:S, and H:M.
/// Returns false and leaves `time` unchanged when input is malformed or out of range.
bool decodeTime(const String &encoded, TimeType &time);
