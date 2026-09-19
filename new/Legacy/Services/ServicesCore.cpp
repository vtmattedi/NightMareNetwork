#include <Services/ServicesCore.h>

void FormatSend(String topic, String payload, String hostname)
{

    String topicWithOwner = "";
    topicWithOwner += hostname;
    if (topic != "" || topic != NULL)
    {
        if (topic[0] != '/')
            topicWithOwner += "/";
        topicWithOwner += topic;
    }
    Send_to_MQTT(topicWithOwner, payload);
}

