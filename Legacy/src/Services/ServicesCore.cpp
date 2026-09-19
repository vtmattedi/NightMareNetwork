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
    // Serial.printf("\x1b[32;1mSending MQTT message:\x1b[0m\n");
    // Serial.printf("<Topic> '%s'\n", topicWithOwner.c_str());
    // Serial.printf("<Payload> '%s'\n", payload.c_str());
    // Serial.printf("------------------------------------------\n");
    Send_to_MQTT(topicWithOwner, payload);
}

#endif