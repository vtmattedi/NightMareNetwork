#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_MQTT
#include <Arduino.h>

// ESP MQTT client and broker lifecycle. Topics arrive unmodified.
namespace NmMqttEsp
{
using MessageHandler = void (*)(const String &topic, const String &payload);
using ConnectedHandler = void (*)(bool lanBroker);
using DisconnectedHandler = void (*)(bool lanBroker);
using ErrorHandler = void (*)(bool lanBroker);

void setHandlers(MessageHandler message, ConnectedHandler connected,
                 DisconnectedHandler disconnected, ErrorHandler error);
bool begin(bool lanBroker);
void end();
void finish();
bool changeTo(bool lanBroker);
bool publish(const char *topic, const uint8_t *payload, size_t length,
             bool retained = false);
bool subscribe(const char *topicFilter);
bool unsubscribe(const char *topicFilter);
bool connected();
bool isLanBroker();
int8_t state(); // -2 connecting, -1 stopped, 0 disconnected, 1 LAN, 2 cloud.
}
#endif // NM_ENABLE_MQTT
