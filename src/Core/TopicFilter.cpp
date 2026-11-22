#include "TopicFilter.h"

#include <cstdint>
#include <cstring>

namespace NightMare
{
bool TopicMatches(std::string_view topic, std::string_view filter)
{
    if (topic.empty() || filter.empty())
        return false;

    // MQTT reserves the '$' prefix: wildcards never reach into those topics.
    const bool reservedTopic = topic.front() == '$';
    size_t t = 0;
    size_t f = 0;
    bool firstLevel = true;

    while (true)
    {
        size_t filterEnd = filter.find('/', f);
        const bool lastFilterLevel = filterEnd == std::string_view::npos;
        if (lastFilterLevel)
            filterEnd = filter.size();
        const std::string_view filterLevel = filter.substr(f, filterEnd - f);

        if (filterLevel == "#")
            return lastFilterLevel && !(firstLevel && reservedTopic);

        size_t topicEnd = topic.find('/', t);
        const bool lastTopicLevel = topicEnd == std::string_view::npos;
        if (lastTopicLevel)
            topicEnd = topic.size();
        const std::string_view topicLevel = topic.substr(t, topicEnd - t);

        if (filterLevel == "+")
        {
            if (firstLevel && reservedTopic)
                return false;
        }
        else if (filterLevel != topicLevel)
        {
            return false;
        }

        if (lastFilterLevel && lastTopicLevel)
            return true;
        // "sport/#" also matches the parent level "sport".
        if (lastTopicLevel)
            return filter.substr(filterEnd) == "/#";
        if (lastFilterLevel)
            return false;

        firstLevel = false;
        f = filterEnd + 1;
        t = topicEnd + 1;
    }
}

bool ValidTopicFilter(const char *topicFilter)
{
    if (topicFilter == nullptr)
        return false;
    const size_t length = strlen(topicFilter);
    if (length == 0 || length > MaxTopicFilterLength)
        return false;

    for (size_t i = 0; i < length; ++i)
    {
        const char c = topicFilter[i];
        if (static_cast<uint8_t>(c) < 0x20)
            return false;
        if (c == '+' && (i != 0 && topicFilter[i - 1] != '/'))
            return false;
        if (c == '+' && (i + 1 != length && topicFilter[i + 1] != '/'))
            return false;
        if (c == '#' && ((i != 0 && topicFilter[i - 1] != '/') || i + 1 != length))
            return false;
    }
    return true;
}
}
