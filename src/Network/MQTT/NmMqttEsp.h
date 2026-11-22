#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_MQTT
#include <Core/Text.h>
#include <Core/PlatformClock.h>

// ESP MQTT client and broker lifecycle. Topics arrive unmodified.
namespace NmMqttEsp
{
using MessageHandler = void (*)(const std::string &topic, const std::string &payload, bool retained);
using ConnectedHandler = void (*)(bool lanBroker);
using DisconnectedHandler = void (*)(bool lanBroker);
using ErrorHandler = void (*)(bool lanBroker);

void setHandlers(MessageHandler message, ConnectedHandler connected,
                 DisconnectedHandler disconnected, ErrorHandler error);
// Whether creds.h describes that broker.
bool profileSupported(bool lanBroker);
// withWill installs <device>/status as the session's Last Will; a session
// without one leaves presence to another connection.
bool begin(bool lanBroker, bool withWill);
// Graceful: publishes the offline status first when the session holds presence.
void end();
void finish();
bool changeTo(bool lanBroker, bool withWill);
bool publish(const char *topic, const uint8_t *payload, size_t length,
             bool retained = false);
bool subscribe(const char *topicFilter);
bool unsubscribe(const char *topicFilter);
bool connected();
// Connected with the status Last Will installed.
bool sessionHoldsPresence();
bool isLanBroker();
int8_t state(); // -2 connecting, -1 stopped, 0 disconnected, 1 LAN, 2 cloud.
}
#endif // NM_ENABLE_MQTT
