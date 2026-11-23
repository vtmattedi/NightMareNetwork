#pragma once

#include <NightMare/Features.h>
#include <stddef.h>

// Pure validation of the namespace ID and the topic root (docs/architecture/namespaces.md).
// constexpr so a bad NM_NAMESPACE / NM_TOPIC_ROOT fails the build and so the rules are
// checked by static_assert in the smoke projects. No state, no dependencies.
namespace NightMare::NamespaceId
{
inline constexpr size_t MaxLength = 16;

constexpr size_t length(const char *text)
{
    size_t n = 0;
    while (text != nullptr && text[n] != '\0')
        ++n;
    return n;
}

/// @brief 1-16 characters from [a-z0-9-], starting with a letter.
constexpr bool validNamespace(const char *id)
{
    const size_t n = length(id);
    if (n == 0 || n > MaxLength || !(id[0] >= 'a' && id[0] <= 'z'))
        return false;
    for (size_t i = 0; i < n; ++i)
    {
        const char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'))
            return false;
    }
    return true;
}

/// @brief One valid topic segment: 1-16 characters, none of '/', '+', '#' or control characters.
constexpr bool validTopicRoot(const char *root)
{
    const size_t n = length(root);
    if (n == 0 || n > MaxLength)
        return false;
    for (size_t i = 0; i < n; ++i)
    {
        const char c = root[i];
        if (c == '/' || c == '+' || c == '#' || static_cast<unsigned char>(c) < 0x20 ||
            static_cast<unsigned char>(c) == 0x7F)
            return false;
    }
    return true;
}
}

// An empty NM_NAMESPACE is the legacy unprefixed layout and is valid as a default.
static_assert(NM_NAMESPACE[0] == '\0' || NightMare::NamespaceId::validNamespace(NM_NAMESPACE),
              "NM_NAMESPACE must be empty or 1-16 characters of [a-z0-9-] starting with a letter");
static_assert(NightMare::NamespaceId::validTopicRoot(NM_TOPIC_ROOT),
              "NM_TOPIC_ROOT must be one topic segment of 1-16 characters without / + # or control characters");
