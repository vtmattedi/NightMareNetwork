#include <Services/ServicesCore.h>


extern void Send_to_MQTT(String, String);

extern void FormatSend(String topic, String payload, String hostname)
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
