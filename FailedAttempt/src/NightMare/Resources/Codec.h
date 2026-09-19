#pragma once
#include <NightMare/Resources/NetResource.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <type_traits>
#include <limits>

namespace NightMare {

template<typename T> struct NetType { static constexpr NetValueType valueType = NetValueType::STRUCT; };
template<> struct NetType<void> { static constexpr NetValueType valueType = NetValueType::NONE; };
#define NM_TYPE(cpp, value) template<> struct NetType<cpp> { static constexpr NetValueType valueType = NetValueType::value; };
NM_TYPE(bool, BOOL)
NM_TYPE(int8_t, INT8)
NM_TYPE(uint8_t, UINT8)
NM_TYPE(int16_t, INT16)
NM_TYPE(uint16_t, UINT16)
NM_TYPE(int32_t, INT32)
NM_TYPE(uint32_t, UINT32)
NM_TYPE(int64_t, INT64)
NM_TYPE(uint64_t, UINT64)
NM_TYPE(float, FLOAT32)
NM_TYPE(double, FLOAT64)
NM_TYPE(String, STRING)
#undef NM_TYPE

// Specialize NetCodec for a structured Action/Event argument. Its schema name
// and field descriptions are supplied at registration through ResourceMetadata.
template<typename T, typename Enable = void> struct NetCodec;

template<typename T>
struct NetCodec<T, typename std::enable_if<std::is_integral<T>::value && !std::is_same<T, bool>::value>::type> {
    static String encode(T value) {
        char buffer[24];
        if (std::is_signed<T>::value) snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
        else snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
        return String(buffer);
    }
    static bool decode(const String& text, T& out) {
        if (!text.length() || text[0] == '-' && !std::is_signed<T>::value) return false;
        char* end = nullptr;
        errno = 0;
        if (std::is_signed<T>::value) {
            long long parsed = strtoll(text.c_str(), &end, 10);
            if (errno || *end || parsed < static_cast<long long>(std::numeric_limits<T>::min()) ||
                parsed > static_cast<long long>(std::numeric_limits<T>::max())) return false;
            out = static_cast<T>(parsed);
        } else {
            unsigned long long parsed = strtoull(text.c_str(), &end, 10);
            if (errno || *end || parsed > static_cast<unsigned long long>(std::numeric_limits<T>::max())) return false;
            out = static_cast<T>(parsed);
        }
        return true;
    }
};

template<> struct NetCodec<bool> {
    static String encode(bool value) { return value ? "true" : "false"; }
    static bool decode(const String& text, bool& out) {
        if (text == "true" || text == "1") { out = true; return true; }
        if (text == "false" || text == "0") { out = false; return true; }
        return false;
    }
};

template<typename T>
struct NetCodec<T, typename std::enable_if<std::is_floating_point<T>::value>::type> {
    static String encode(T value) { return String(value, std::is_same<T, float>::value ? 6 : 12); }
    static bool decode(const String& text, T& out) {
        if (!text.length()) return false;
        char* end = nullptr;
        errno = 0;
        double parsed = strtod(text.c_str(), &end);
        if (errno || *end || !isfinite(parsed)) return false;
        out = static_cast<T>(parsed);
        return isfinite(out);
    }
};

template<> struct NetCodec<String> {
    // A one-byte marker preserves an empty String on retained MQTT state topics.
    static String encode(const String& value) { return "~" + value; }
    static bool decode(const String& text, String& out) {
        if (!text.startsWith("~")) return false;
        out = text.substring(1);
        return true;
    }
};

} // namespace NightMare
