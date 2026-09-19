#pragma once
#include <NightMare/Resources/NetResource.h>
#include <string.h>

#ifndef NIGHTMARE_MAX_RESOURCES
#define NIGHTMARE_MAX_RESOURCES 32
#endif

namespace NightMare {

struct PublishPolicy {
    bool onChange = true;
    uint32_t periodMs = 0;
};

class ResourceRegistry {
public:
    struct Entry {
        NetResource* resource = nullptr;
        const char* owner = nullptr; // Caller-owned, only used for remote resources.
        PublishPolicy policy;
        // Local: last publication. Remote Value: last valid state reception.
        uint32_t lastActivityMs = 0;
        ResourceRole role = ResourceRole::LOCAL;
        bool hasReceivedState = false;
    };

    bool add(NetResource& resource, ResourceRole role = ResourceRole::LOCAL,
             const char* owner = nullptr, PublishPolicy policy = {}) {
        if (!validId(resource.id()) ||
            count() >= NIGHTMARE_MAX_RESOURCES ||
            (role == ResourceRole::REMOTE && !validId(owner))) return false;
        if (find(role, owner, resource.id())) return false;
        for (auto& entry : _entries) {
            if (!entry.resource) {
                entry.resource = &resource;
                entry.role = role;
                entry.owner = role == ResourceRole::REMOTE ? owner : nullptr;
                entry.policy = policy;
                entry.lastActivityMs = 0;
                entry.hasReceivedState = false;
                return true;
            }
        }
        return false;
    }

    Entry* find(ResourceRole role, const char* owner, const String& id) {
        for (auto& entry : _entries)
            if (entry.resource && entry.role == role && entry.resource->id() == id &&
                (role == ResourceRole::LOCAL || (owner && strcmp(entry.owner, owner) == 0))) return &entry;
        return nullptr;
    }
    Entry* find(const NetResource& resource) {
        for (auto& entry : _entries) if (entry.resource == &resource) return &entry;
        return nullptr;
    }
    const Entry* find(const NetResource& resource) const {
        for (const auto& entry : _entries) if (entry.resource == &resource) return &entry;
        return nullptr;
    }
    Entry (&entries())[NIGHTMARE_MAX_RESOURCES] { return _entries; }
    const Entry (&entries() const)[NIGHTMARE_MAX_RESOURCES] { return _entries; }
    size_t count() const {
        size_t n = 0;
        for (const auto& entry : _entries) if (entry.resource) ++n;
        return n;
    }
private:
    static bool validId(const char* id) {
        if (!id || !*id) return false;
        size_t length = 0;
        for (const char* ch = id; *ch; ++ch, ++length) {
            if (length >= 64 || !((*ch >= 'a' && *ch <= 'z') ||
                                  (*ch >= 'A' && *ch <= 'Z') ||
                                  (*ch >= '0' && *ch <= '9') ||
                                  *ch == '_' || *ch == '-' || *ch == '.')) return false;
        }
        return true;
    }
    static bool validId(const String& id) { return validId(id.c_str()); }
    Entry _entries[NIGHTMARE_MAX_RESOURCES];
};

} // namespace NightMare
