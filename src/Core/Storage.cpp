#include "Storage.h"

#include <dirent.h>
#include <esp_spiffs.h>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace NightMare
{
namespace Storage
{
namespace
{
constexpr char MountPoint[] = "/nm";
bool mounted = false;

std::string physicalPath(const std::string &path)
{
    if (path.empty() || path[0] != '/' ||
        path.find("/../") != std::string::npos ||
        (path.size() >= 3 && path.compare(path.size() - 3, 3, "/..") == 0))
        return {};
    return std::string(MountPoint) + path;
}

bool fileExists(const std::string &name)
{
    struct stat info;
    return stat(name.c_str(), &info) == 0;
}

void recoverPrevious(const std::string &name)
{
    if (!fileExists(name) && fileExists(name + ".bak"))
        std::rename((name + ".bak").c_str(), name.c_str());
}
}

bool mount()
{
    if (mounted)
        return true;
    esp_vfs_spiffs_conf_t config{};
    config.base_path = MountPoint;
    config.partition_label = nullptr;
    config.max_files = 8;
    config.format_if_mount_failed = true;
    mounted = esp_vfs_spiffs_register(&config) == ESP_OK;
    return mounted;
}

bool exists(const std::string &path)
{
    if (!mount())
        return false;
    const std::string name = physicalPath(path);
    if (name.empty())
        return false;
    recoverPrevious(name);
    return fileExists(name);
}

bool read(const std::string &path, std::string &out, size_t maxBytes)
{
    out.clear();
    if (!mount())
        return false;
    const std::string name = physicalPath(path);
    if (name.empty())
        return false;
    recoverPrevious(name);
    struct stat info;
    if (stat(name.c_str(), &info) != 0 || info.st_size < 0 ||
        static_cast<size_t>(info.st_size) > maxBytes)
        return false;
    FILE *file = std::fopen(name.c_str(), "rb");
    if (file == nullptr)
        return false;
    out.resize(static_cast<size_t>(info.st_size));
    const size_t readBytes = out.empty() ? 0 : std::fread(&out[0], 1, out.size(), file);
    const bool complete = readBytes == out.size() && !std::ferror(file);
    std::fclose(file);
    if (!complete)
        out.clear();
    return complete;
}

bool write(const std::string &path, const std::string &data, bool atomic)
{
    if (!mount())
        return false;
    const std::string name = physicalPath(path);
    if (name.empty())
        return false;
    recoverPrevious(name);
    const std::string target = atomic ? name + ".tmp" : name;
    FILE *file = std::fopen(target.c_str(), "wb");
    if (file == nullptr)
        return false;
    const bool written = std::fwrite(data.data(), 1, data.size(), file) == data.size();
    const bool flushed = std::fflush(file) == 0;
    const bool closed = std::fclose(file) == 0;
    const bool complete = written && flushed && closed;
    if (!complete)
    {
        std::remove(target.c_str());
        return false;
    }
    if (atomic)
    {
        const std::string backup = name + ".bak";
        const bool hadPrevious = fileExists(name);
        if (hadPrevious &&
            ((fileExists(backup) && std::remove(backup.c_str()) != 0) ||
             std::rename(name.c_str(), backup.c_str()) != 0))
        {
            std::remove(target.c_str());
            return false;
        }
        if (std::rename(target.c_str(), name.c_str()) != 0)
        {
            if (hadPrevious)
                std::rename(backup.c_str(), name.c_str());
            std::remove(target.c_str());
            return false;
        }
        if (hadPrevious)
            std::remove(backup.c_str());
    }
    return true;
}

bool remove(const std::string &path)
{
    const std::string name = physicalPath(path);
    return mount() && !name.empty() && std::remove(name.c_str()) == 0;
}

bool rename(const std::string &from, const std::string &to)
{
    const std::string source = physicalPath(from);
    const std::string destination = physicalPath(to);
    return mount() && !source.empty() && !destination.empty() &&
           std::rename(source.c_str(), destination.c_str()) == 0;
}

bool info(size_t &totalBytes, size_t &usedBytes)
{
    return mount() && esp_spiffs_info(nullptr, &totalBytes, &usedBytes) == ESP_OK;
}

bool format()
{
    return mount() && esp_spiffs_format(nullptr) == ESP_OK;
}

bool list(std::vector<Entry> &entries)
{
    entries.clear();
    if (!mount())
        return false;
    DIR *dir = opendir(MountPoint);
    if (dir == nullptr)
        return false;
    while (dirent *item = readdir(dir))
    {
        std::string name = std::string("/") + item->d_name;
        const std::string physical = physicalPath(name);
        struct stat info;
        if (!physical.empty() && stat(physical.c_str(), &info) == 0)
            entries.push_back({name, static_cast<size_t>(info.st_size)});
    }
    closedir(dir);
    return true;
}
}
}
