#pragma once

#include <NightMare/Features.h>
#if NM_ENABLE_NETWORK

#include <Arduino.h>
#include <Network/NmConnection.h>

namespace NightMare
{
struct GatewayCandidateStatus
{
    bool known = false;
    String id;
    String generation;
    bool espNowReady = false;
    bool remoteMqttReady = false;
    bool localMqttReady = false;
    String ssid;
    String bssid;
    uint8_t channel = 0;
};

struct GatewayRadioContext
{
    String ssid;
    String bssid;
    uint8_t channel = 0;
};

// Pure policy seam used by routing tests. A gateway announcement is only
// readiness evidence; the ESP-NOW handshake remains the authentication step.
bool GatewayCandidateProbable(const GatewayCandidateStatus &candidate,
                              MqttProfile requiredUplink,
                              const GatewayRadioContext &radio);

// Consumes retained <gateway-id>/gateway/network announcements. False means
// the message was not a gateway contract message or was malformed.
bool GatewayCandidateHandleMessage(const String &topic, const String &payload,
                                   bool retained);
GatewayCandidateStatus GatewayCandidateGet();
bool GatewayCandidateIsProbable(MqttProfile requiredUplink);
bool GatewayCandidateMatchesAuthenticated(const char *gatewayId);
const char *GatewayNetworkTopicFilter();
const char *GatewayStatusTopicFilter();
}

#endif
