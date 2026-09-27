class TimeType {
private:
    int hours_;
    int minutes_;
    int seconds_;
public:
    TimeType(int h, int m, int s);
    TimeType(const String &timeStr);
    TimeType(const time_t &epochTime = now());
    String toString() const;
    TimeType &operator=(const TimeType &other);
    bool operator==(const TimeType &other) const;
    bool operator==(const time_t &other) const;
};
// Codec
// Converts a TimeType to a string representation in the format: HH:MM:SS
// Same as TimeType::toString(), but static and can be used without an instance of TimeType.
String encodeTime(const TimeType &time);
// Converts a string representation of a time into a TimeType object.
// Supported formats: HH:MM:SS, HH:MM, HHMMSS, HHMM, H:M:S, H:M
bool decodeTime(const String &timeStr, TimeType &time);