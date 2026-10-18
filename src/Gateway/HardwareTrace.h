#pragma once
#include <Core/Logs.h>
#include <Gateway/Broker/Topic.h>
#include <string>

// <device>/hardware (and anything under it) is the large retained document each
// device publishes at boot. Every hop one takes through the gateway is logged on
// this module, so a document that goes missing, loses its retained flag or
// never gets its PUBACK can be followed end to end:
//
//   device -> gateway   "rx espnow"      (espBroker_onMessage)
//   gateway vault       "vault"          (retainIfPersistent)
//   gateway -> broker   "mqtt tx/queue/PUBACK"  (espMqtt)
//   broker -> gateway   "mqtt rx"        (espMqtt event handler)
//   gateway -> device   "tx espnow"      (espBroker_sendMessage)
//
// Logged at info level, and switched at runtime with
// NightMareGateway_setHardwareTrace() (on by default): NightMare log levels
// are compile-time only.
#define HW_TAG "hardware"

void NightMareGateway_setHardwareTrace(bool enabled);
bool NightMareGateway_hardwareTrace();

inline bool isHardwareTopic(const std::string &topic)
{
    return topicMatchesPattern(topic, "+/hardware") || topicMatchesPattern(topic, "+/hardware/#");
}

#define HW_TRACE(fmt, ...)                         \
    do                                             \
    {                                              \
        if (NightMareGateway_hardwareTrace())      \
            LOG(HW_TAG, fmt, ##__VA_ARGS__);       \
    } while (0)
