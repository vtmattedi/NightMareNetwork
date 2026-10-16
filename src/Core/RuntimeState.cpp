#include "RuntimeState.h"

#include <stdio.h>

namespace
{
void appendJsonString(std::string &output, const std::string &input)
{
    output += '"';
    for (size_t i = 0; i < input.length(); ++i)
    {
        const char c = input[i];
        if (c == '"' || c == '\\')
        {
            output += '\\';
            output += c;
        }
        else if (static_cast<uint8_t>(c) < 0x20)
        {
            char escaped[7];
            snprintf(escaped, sizeof(escaped), "\\u%04x",
                     static_cast<unsigned int>(static_cast<uint8_t>(c)));
            output += escaped;
        }
        else
        {
            output += c;
        }
    }
    output += '"';
}
}

bool RuntimeState::set(const std::string &key, const std::string &value)
{
    if (key.length() == 0)
        return false;
    for (size_t i = 0; i < count_; ++i)
    {
        if (entries_[i].key == key)
        {
            entries_[i].value = value;
            return true;
        }
    }
    if (count_ >= MaxEntries)
        return false;
    entries_[count_].key = key;
    entries_[count_].value = value;
    ++count_;
    return true;
}

std::string RuntimeState::get(const std::string &key, const std::string &defaultValue) const
{
    for (size_t i = 0; i < count_; ++i)
        if (entries_[i].key == key)
            return entries_[i].value;
    return defaultValue;
}

bool RuntimeState::setFlag(const std::string &key, bool value)
{
    return set(key, value ? "1" : "0");
}

bool RuntimeState::getFlag(const std::string &key) const
{
    return get(key, "0") == "1";
}

bool RuntimeState::exists(const std::string &key) const
{
    for (size_t i = 0; i < count_; ++i)
        if (entries_[i].key == key)
            return true;
    return false;
}

bool RuntimeState::remove(const std::string &key)
{
    for (size_t i = 0; i < count_; ++i)
    {
        if (entries_[i].key != key)
            continue;
        for (size_t j = i + 1; j < count_; ++j)
            entries_[j - 1] = entries_[j];
        entries_[--count_] = Entry{};
        return true;
    }
    return false;
}

void RuntimeState::clear()
{
    while (count_ != 0)
        entries_[--count_] = Entry{};
}

std::string RuntimeState::toJson() const
{
    std::string output = "{";
    for (size_t i = 0; i < count_; ++i)
    {
        if (i != 0)
            output += ',';
        appendJsonString(output, entries_[i].key);
        output += ':';
        appendJsonString(output, entries_[i].value);
    }
    output += '}';
    return output;
}
