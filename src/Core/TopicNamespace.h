#pragma once

#include <Core/TopicFilter.h>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// The namespace prefix `<root>/<ns>/` of the MQTT layout (docs/architecture/namespaces.md).
// Topics are namespace-relative everywhere inside the library; the prefix is added and
// removed only at the two MQTT client boundaries (Network/MQTT/NmMqttEsp.cpp and
// Gateway/EspMqtt/espMqtt.cpp). With an empty namespace (legacy) every function here is
// the identity, so behavior is unchanged.
namespace NightMare::TopicNamespace
{
// --- Pure rules: explicit root and namespace, usable in static_assert. ---

/// @brief Length of "<root>/<ns>/"; 0 for an empty (legacy) namespace.
constexpr size_t prefixLength(std::string_view root, std::string_view ns)
{
    return ns.empty() ? 0 : root.size() + 1 + ns.size() + 1;
}

/// @brief Whether `full` starts with "<root>/<ns>/". Always true for an empty namespace.
constexpr bool underPrefix(std::string_view root, std::string_view ns, std::string_view full)
{
    if (ns.empty())
        return true;
    const size_t n = prefixLength(root, ns);
    if (full.size() < n)
        return false;
    return full.substr(0, root.size()) == root && full[root.size()] == '/' &&
           full.substr(root.size() + 1, ns.size()) == ns && full[n - 1] == '/';
}

/// @brief Removes the prefix. False (out untouched) when `full` is not under it.
constexpr bool stripView(std::string_view root, std::string_view ns, std::string_view full,
                         std::string_view &out)
{
    if (!underPrefix(root, ns, full))
        return false;
    out = full.substr(prefixLength(root, ns));
    return true;
}

/// @brief Length of applyPrefix() for a relative topic or filter of `relativeLength` bytes.
constexpr size_t prefixedLength(std::string_view root, std::string_view ns, size_t relativeLength)
{
    return prefixLength(root, ns) + relativeLength;
}

/// @brief The prefixed topic or filter ("+/status" becomes "nm/home/+/status", "#" becomes
/// "nm/home/#"). The limit check is separate: see fitsMqttLimit().
inline std::string applyPrefix(std::string_view root, std::string_view ns, std::string_view relative)
{
    std::string full;
    full.reserve(prefixedLength(root, ns, relative.size()));
    if (!ns.empty())
    {
        full.append(root);
        full.push_back('/');
        full.append(ns);
        full.push_back('/');
    }
    full.append(relative);
    return full;
}

/// @brief The 192-character MQTT filter/topic limit applies to the FULL topic.
constexpr bool fitsMqttLimit(size_t fullLength)
{
    return fullLength != 0 && fullLength <= MaxTopicFilterLength;
}

// --- With this device's NightMare::Namespace() / TopicRoot(). ---

/// @brief Relative topic to the full MQTT topic.
std::string applyPrefix(std::string_view relative);
/// @brief Relative filter to the full MQTT filter. Same rule as applyPrefix(): wildcards in the
/// first level are kept, only the prefix is put in front.
std::string applyFilterPrefix(std::string_view filter);
/// @brief Full topic to the relative one. False when the topic is not under the prefix.
bool stripPrefix(std::string_view full, std::string &relative);

// --- Messages that arrive outside the prefix. ---

/// @brief Counts a received message that was under neither the prefix nor a global filter.
void noteForeignDropped();
/// @brief How many such messages were dropped since boot (debug counter).
uint32_t foreignDroppedCount();
}
