#include <Services/ServicesCore.h>
#ifdef COMPILE_SERVICES

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
    // Serial.printf("'%s' --> '%s' [%s]\n",topic.c_str(), topicWithOwner.c_str(),hostname.c_str());
    Send_to_MQTT(topicWithOwner, payload);
}

#endif