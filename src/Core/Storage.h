#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace NightMare
{
namespace Storage
{
struct Entry
{
    std::string name;
    size_t size;
};

bool mount();
bool exists(const std::string &path);
bool read(const std::string &path, std::string &out, size_t maxBytes);
bool write(const std::string &path, const std::string &data, bool atomic = false);
bool remove(const std::string &path);
bool rename(const std::string &from, const std::string &to);
bool info(size_t &totalBytes, size_t &usedBytes);
bool format();
bool list(std::vector<Entry> &entries);
}
}
