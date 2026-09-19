#include <NightMare/Storage/SettingsStore.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

namespace NightMare {

SettingsStore::SettingsStore(const char* name, StorageMode mode, SettingsPersistence* persistence)
    : _name(name), _mode(mode), _persistence(persistence) {}

bool SettingsStore::begin() {
    if (_begun) return true;
    if (_mode == StorageMode::Persistent) {
        if (!_persistence) return false;
        _loading = true;
        bool loaded = _persistence->load(*this);
        _loading = false;
        if (!loaded) return false;
    }
    _begun = true;
    return true;
}

bool SettingsStore::allowed(const char* key, SettingsAccess access) const {
    if (!key || !*key) return false;
    size_t length = strlen(key);
    return length <= 64 && (access == SettingsAccess::Internal || key[0] != '_');
}

int SettingsStore::find(const char* key) const {
    if (!key) return -1;
    for (size_t i = 0; i < _count; ++i)
        if (_entries[i].key == key) return static_cast<int>(i);
    return -1;
}

bool SettingsStore::flush() {
    return _loading || _mode == StorageMode::Memory ||
           (_persistence && _persistence->save(*this));
}

bool SettingsStore::setString(const char* key, const String& value, SettingsAccess access) {
    if (!allowed(key, access) || (!_begun && !_loading) || value.length() > 512) return false;
    int index = find(key);
    if (index >= 0 && _entries[index].value == value) return true;
    bool added = index < 0;
    if (added) {
        if (_count == NIGHTMARE_MAX_SETTINGS) return false;
        index = static_cast<int>(_count++);
        _entries[index].key = key;
    }
    String previous = _entries[index].value;
    _entries[index].value = value;
    if (flush()) return true;
    if (added) _entries[--_count] = {};
    else _entries[index].value = previous;
    return false;
}

bool SettingsStore::setBool(const char* key, bool value, SettingsAccess access) {
    return setString(key, value ? "1" : "0", access);
}
bool SettingsStore::setInt(const char* key, int32_t value, SettingsAccess access) {
    return setString(key, String(value), access);
}
bool SettingsStore::setUInt(const char* key, uint32_t value, SettingsAccess access) {
    return setString(key, String(value), access);
}
bool SettingsStore::setFloat(const char* key, float value, SettingsAccess access) {
    return isfinite(value) && setString(key, String(value, 6), access);
}

String SettingsStore::getString(const char* key, const String& fallback, SettingsAccess access) const {
    if (!allowed(key, access)) return fallback;
    int index = find(key);
    return index < 0 ? fallback : _entries[index].value;
}

bool SettingsStore::getBool(const char* key, bool fallback, SettingsAccess access) const {
    if (!exists(key, access)) return fallback;
    String value = getString(key, "", access);
    if (value == "1" || value == "true") return true;
    if (value == "0" || value == "false") return false;
    return fallback;
}

int32_t SettingsStore::getInt(const char* key, int32_t fallback, SettingsAccess access) const {
    if (!exists(key, access)) return fallback;
    String value = getString(key, "", access);
    if (!value.length()) return fallback;
    char* end = nullptr;
    errno = 0;
    long parsed = strtol(value.c_str(), &end, 10);
    return errno || *end || parsed < INT32_MIN || parsed > INT32_MAX ? fallback : static_cast<int32_t>(parsed);
}

uint32_t SettingsStore::getUInt(const char* key, uint32_t fallback, SettingsAccess access) const {
    if (!exists(key, access)) return fallback;
    String value = getString(key, "", access);
    if (!value.length() || value[0] == '-') return fallback;
    char* end = nullptr;
    errno = 0;
    unsigned long parsed = strtoul(value.c_str(), &end, 10);
    return errno || *end || parsed > UINT32_MAX ? fallback : static_cast<uint32_t>(parsed);
}

float SettingsStore::getFloat(const char* key, float fallback, SettingsAccess access) const {
    if (!exists(key, access)) return fallback;
    String value = getString(key, "", access);
    if (!value.length()) return fallback;
    char* end = nullptr;
    errno = 0;
    float parsed = strtof(value.c_str(), &end);
    return errno || *end || !isfinite(parsed) ? fallback : parsed;
}

bool SettingsStore::exists(const char* key, SettingsAccess access) const {
    return allowed(key, access) && find(key) >= 0;
}

bool SettingsStore::remove(const char* key, SettingsAccess access) {
    if (!allowed(key, access) || !_begun) return false;
    int index = find(key);
    if (index < 0) return false;
    Entry removed = _entries[index];
    for (size_t i = index; i + 1 < _count; ++i) _entries[i] = _entries[i + 1];
    _entries[--_count] = {};
    if (flush()) return true;
    for (size_t i = _count; i > static_cast<size_t>(index); --i) _entries[i] = _entries[i - 1];
    _entries[index] = removed;
    ++_count;
    return false;
}

bool SettingsStore::clear(SettingsAccess access) {
    if (!_begun) return false;
    Entry backup[NIGHTMARE_MAX_SETTINGS];
    size_t original = _count;
    for (size_t i = 0; i < _count; ++i) backup[i] = _entries[i];
    size_t write = 0;
    for (size_t i = 0; i < _count; ++i)
        if (access == SettingsAccess::User && _entries[i].key.startsWith("_"))
            _entries[write++] = _entries[i];
    for (size_t i = write; i < _count; ++i) _entries[i] = {};
    _count = write;
    if (flush()) return true;
    for (size_t i = 0; i < original; ++i) _entries[i] = backup[i];
    _count = original;
    return false;
}

void SettingsStore::visit(Visitor visitor, void* context, SettingsAccess access) const {
    if (!visitor) return;
    for (size_t i = 0; i < _count; ++i)
        if (access == SettingsAccess::Internal || !_entries[i].key.startsWith("_"))
            visitor(context, _entries[i].key, _entries[i].value);
}

size_t SettingsStore::count(SettingsAccess access) const {
    if (access == SettingsAccess::Internal) return _count;
    size_t result = 0;
    for (size_t i = 0; i < _count; ++i) if (!_entries[i].key.startsWith("_")) ++result;
    return result;
}

} // namespace NightMare
