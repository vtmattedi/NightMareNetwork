#include <NightMare/Features.h>
#if NM_ENABLE_GATEWAY
#include <Gateway/Broker/Topic.h>
#include <Core/TopicFilter.h>

bool isValidPublishTopic(std::string_view topic)
{
    if (topic.empty() || topic.size() > NightMare::MaxTopicLength)
        return false;

    for (char c : topic)
    {
        if (c == '+' || c == '#' || c == '\0')
            return false;
    }
    return true;
}

bool topicMatchesPattern(std::string_view topic, std::string_view filter)
{
    return NightMare::TopicMatches(topic, filter);
}

#endif // NM_ENABLE_GATEWAY
