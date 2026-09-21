#pragma once

#include <Arduino.h>

// Small in-memory status store. It is part of core and never accesses storage.
class RuntimeState
{
public:
    static constexpr size_t MaxEntries = 64;
    virtual ~RuntimeState() = default;

    virtual bool set(const String &key, const String &value);
    virtual String get(const String &key, const String &defaultValue = "") const;
    bool setFlag(const String &key, bool value);
    bool getFlag(const String &key) const;
    virtual bool exists(const String &key) const;
    virtual bool remove(const String &key);
    virtual void clear();
    virtual size_t size() const { return count_; }
    virtual String toJson() const;

private:
    struct Entry
    {
        String key;
        String value;
    };
    Entry entries_[MaxEntries];
    size_t count_ = 0;
};

extern RuntimeState SystemState;
