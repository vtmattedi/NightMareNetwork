#pragma once

#include <Arduino.h>

// ESP MQTT client and broker lifecycle. Topics arrive unmodified.
namespace NmMqttEsp
{
using MessageHandler = void (*)(const String &topic, const String &payload);
using ConnectedHandler = void (*)(bool lanBroker);
using DisconnectedHandler = void (*)(bool lanBroker);

void setHandlers(MessageHandler message, ConnectedHandler connected,
                 DisconnectedHandler disconnected);
bool begin(bool lanBroker);
void end();
void finish();
bool changeTo(bool lanBroker);
bool publish(const String &topic, const String &payload, bool retained = false);
bool connected();
bool isLanBroker();
int8_t state(); // -2 connecting, -1 stopped, 0 disconnected, 1 LAN, 2 cloud.
}
