#pragma once

#include <Arduino.h>

#include <errno.h>
#include <limits>
#include <stdio.h>
#include <stdlib.h>
#include <type_traits>

// Wire taxonomy for values. It lives here, next to the T -> NetValueType
// mapping, so NetResources.h can include this header without a cycle. Actions
// reuse it to describe their argument types at runtime.
enum class NetValueType : uint8_t
{
    STRING,
    BOOLEAN,
    INTEGER,
    FLOAT,
    STRUCT
};

/// @brief Translates a value between its C++ type and the String used on the
/// wire. This is the typed boundary for NetValue<T> only: actions describe their
/// arguments with runtime metadata instead of a single T.
/// Left undefined on purpose: an unsupported T fails to compile at the codec
/// rather than silently degrading to a String somewhere deeper in the stack.
template <typename T, typename Enable = void>
struct NetCodec;

namespace NetCodecDetail
{
    // Signed and unsigned need different parse/print calls, and C++11 has no
    // if constexpr, so the two paths are separate types selected by tag.
    template <bool IsSigned>
    struct IntegerText;

    template <>
    struct IntegerText<true>
    {
        template <typename T>
        static String encode(T value)
        {
            char buffer[24];
            snprintf(buffer, sizeof(buffer), "%lld", (long long)value);
            return String(buffer);
        }

        template <typename T>
        static bool decode(const String &text, T &out)
        {
            const char *begin = text.c_str();
            char *end = nullptr;
            errno = 0;
            const long long parsed = strtoll(begin, &end, 10);
            if (end == begin || *end != '\0' || errno == ERANGE)
                return false;
            if (parsed < (long long)(std::numeric_limits<T>::min)() ||
                parsed > (long long)(std::numeric_limits<T>::max)())
                return false;
            out = (T)parsed;
            return true;
        }
    };

    template <>
    struct IntegerText<false>
    {
        template <typename T>
        static String encode(T value)
        {
            char buffer[24];
            snprintf(buffer, sizeof(buffer), "%llu", (unsigned long long)value);
            return String(buffer);
        }

        template <typename T>
        static bool decode(const String &text, T &out)
        {
            const char *begin = text.c_str();
            if (*begin == '-') // strtoull wraps negatives instead of rejecting them.
                return false;
            char *end = nullptr;
            errno = 0;
            const unsigned long long parsed = strtoull(begin, &end, 10);
            if (end == begin || *end != '\0' || errno == ERANGE)
                return false;
            if (parsed > (unsigned long long)(std::numeric_limits<T>::max)())
                return false;
            out = (T)parsed;
            return true;
        }
    };

    // Shortest representation that still decodes back to the same value, so
    // payloads stay readable ("23.5") without a round trip changing the value
    // and looking like a remote update.
    inline String encodeReal(double value, int maxPrecision, bool narrowToFloat)
    {
        char buffer[40];
        for (int precision = 6; precision < maxPrecision; ++precision)
        {
            snprintf(buffer, sizeof(buffer), "%.*g", precision, value);
            const double parsed = strtod(buffer, nullptr);
            if (narrowToFloat ? ((float)parsed == (float)value) : (parsed == value))
                return String(buffer);
        }
        snprintf(buffer, sizeof(buffer), "%.*g", maxPrecision, value);
        return String(buffer);
    }
}

template <>
struct NetCodec<bool>
{
    static constexpr NetValueType Type = NetValueType::BOOLEAN;

    static String encode(const bool &value) { return value ? "true" : "false"; }

    static bool decode(const String &encoded, bool &out)
    {
        if (encoded.equalsIgnoreCase("true") || encoded == "1")
        {
            out = true;
            return true;
        }
        if (encoded.equalsIgnoreCase("false") || encoded == "0")
        {
            out = false;
            return true;
        }
        return false;
    }
};

template <typename T>
struct NetCodec<T, typename std::enable_if<std::is_integral<T>::value &&
                                           !std::is_same<T, bool>::value>::type>
{
    static constexpr NetValueType Type = NetValueType::INTEGER;

    static String encode(const T &value)
    {
        return NetCodecDetail::IntegerText<std::is_signed<T>::value>::encode(value);
    }

    static bool decode(const String &encoded, T &out)
    {
        if (encoded.length() == 0)
            return false;
        return NetCodecDetail::IntegerText<std::is_signed<T>::value>::decode(encoded, out);
    }
};

template <typename T>
struct NetCodec<T, typename std::enable_if<std::is_floating_point<T>::value>::type>
{
    static constexpr NetValueType Type = NetValueType::FLOAT;

    static String encode(const T &value)
    {
        const bool narrowToFloat = std::is_same<T, float>::value;
        return NetCodecDetail::encodeReal((double)value, narrowToFloat ? 9 : 17, narrowToFloat);
    }

    static bool decode(const String &encoded, T &out)
    {
        const char *begin = encoded.c_str();
        if (*begin == '\0')
            return false;
        char *end = nullptr;
        const double parsed = strtod(begin, &end);
        if (end == begin || *end != '\0')
            return false;
        out = (T)parsed;
        return true;
    }
};

template <>
struct NetCodec<String>
{
    static constexpr NetValueType Type = NetValueType::STRING;

    static String encode(const String &value) { return value; }

    static bool decode(const String &encoded, String &out)
    {
        out = encoded;
        return true;
    }
};
