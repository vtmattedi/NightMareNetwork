#pragma once

#include <Core/Text.h>
#include <Core/PlatformClock.h>

// Generic in-memory std::string key/value store. It never accesses storage.
class RuntimeState
{
public:
    // PersistentSettings shares this store with up to 64 Config declarations.
    // Keep equal additional headroom for framework and application settings.
    static constexpr size_t MaxEntries = 128;
    virtual ~RuntimeState() = default;

    virtual bool set(const std::string &key, const std::string &value);
    virtual std::string get(const std::string &key, const std::string &defaultValue = "") const;
    bool setFlag(const std::string &key, bool value);
    bool getFlag(const std::string &key) const;
    virtual bool exists(const std::string &key) const;
    virtual bool remove(const std::string &key);
    virtual void clear();
    virtual size_t size() const { return count_; }
    virtual std::string toJson() const;

private:
    struct Entry
    {
        std::string key;
        std::string value;
    };
    Entry entries_[MaxEntries];
    size_t count_ = 0;
};
