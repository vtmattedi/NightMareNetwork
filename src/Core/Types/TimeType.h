#pragma once

#include <Arduino.h>

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
