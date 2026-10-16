#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <esp_heap_caps.h>

namespace NightMare
{
namespace Text
{
inline bool tryReserve(std::string &value, size_t capacity)
{
    if (capacity <= value.capacity())
        return true;
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < capacity + 1)
        return false;
    value.reserve(capacity);
    return value.capacity() >= capacity;
}

inline std::string slice(const std::string &value, size_t first, size_t last = std::string::npos)
{
    return value.substr(first, last == std::string::npos ? last : last - first);
}

inline int indexOf(const std::string &value, char needle, size_t from = 0)
{
    const size_t position = value.find(needle, from);
    return position == std::string::npos ? -1 : static_cast<int>(position);
}

inline int lastIndexOf(const std::string &value, char needle)
{
    const size_t position = value.rfind(needle);
    return position == std::string::npos ? -1 : static_cast<int>(position);
}

inline bool startsWith(const std::string &value, const std::string &prefix)
{
    return value.compare(0, prefix.size(), prefix) == 0;
}

inline bool endsWith(const std::string &value, const std::string &suffix)
{
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline bool equalsIgnoreCase(const std::string &left, const std::string &right)
{
    if (left.size() != right.size())
        return false;
    for (size_t i = 0; i < left.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(left[i])) !=
            std::tolower(static_cast<unsigned char>(right[i])))
            return false;
    return true;
}

inline void trim(std::string &value)
{
    const auto nonSpace = [](unsigned char c) { return !std::isspace(c); };
    const auto first = std::find_if(value.begin(), value.end(), nonSpace);
    const auto last = std::find_if(value.rbegin(), value.rend(), nonSpace).base();
    if (first >= last)
        value.clear();
    else
        value = std::string(first, last);
}

inline void toUpper(std::string &value)
{
    for (char &c : value)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

inline void toLower(std::string &value)
{
    for (char &c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

inline void replaceAll(std::string &value, const std::string &from, const std::string &to)
{
    if (from.empty())
        return;
    size_t position = 0;
    while ((position = value.find(from, position)) != std::string::npos)
    {
        value.replace(position, from.size(), to);
        position += to.size();
    }
}

inline long toLong(const std::string &value)
{
    return std::strtol(value.c_str(), nullptr, 10);
}

inline float toFloat(const std::string &value)
{
    return std::strtof(value.c_str(), nullptr);
}

inline std::string hex(uint32_t value)
{
    char buffer[9];
    std::snprintf(buffer, sizeof(buffer), "%x", static_cast<unsigned>(value));
    return buffer;
}
}
}
