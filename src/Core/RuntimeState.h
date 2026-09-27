#pragma once

#include <Arduino.h>

// Generic in-memory String key/value store. It never accesses storage.
class RuntimeState
{
public:
    // PersistentSettings shares this store with up to 64 Config declarations.
    // Keep equal additional headroom for framework and application settings.
    static constexpr size_t MaxEntries = 128;
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
