#pragma once

#include <cstddef>
#include <string_view>

// MQTT topic-filter rules shared by the device connections and the gateway
// broker. No feature dependency: both sides of the network need them.
namespace NightMare
{
// Longest topic filter accepted by Subscribe()/SubscribeOn().
constexpr size_t MaxTopicFilterLength = 192;

// MQTT 3.1.1 matching: '+' is one level, a trailing '#' is any number of
// levels including the parent ("a/#" matches "a"), and wildcards in the first
// level never reach topics that start with '$'.
bool TopicMatches(std::string_view topic, std::string_view filter);

// Non-empty, at most MaxTopicFilterLength characters, no control characters,
// '+' only as a whole level and '#' only as the whole last level.
bool ValidTopicFilter(const char *topicFilter);
}
