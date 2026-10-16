#pragma once

#include <NightMare/Features.h>
#if NM_ENABLE_NETWORK

#include <Core/Text.h>
#include <Core/PlatformClock.h>
#include <Network/NmConnection.h>

namespace NightMare
{
struct GatewayCandidateStatus
{
    bool known = false;
    std::string id;
    std::string generation;
    bool espNowReady = false;
    bool remoteMqttReady = false;
    bool localMqttReady = false;
    std::string ssid;
    std::string bssid;
    uint8_t channel = 0;
};

struct GatewayRadioContext
{
    std::string ssid;
    std::string bssid;
    uint8_t channel = 0;
};

// Pure policy seam used by routing tests. A gateway announcement is only
// readiness evidence; the ESP-NOW handshake remains the authentication step.
bool GatewayCandidateProbable(const GatewayCandidateStatus &candidate,
                              MqttProfile requiredUplink,
                              const GatewayRadioContext &radio);

// Consumes retained <gateway-id>/gateway/network announcements. False means
// the message was not a gateway contract message or was malformed.
bool GatewayCandidateHandleMessage(const std::string &topic, const std::string &payload,
                                   bool retained);
GatewayCandidateStatus GatewayCandidateGet();
bool GatewayCandidateIsProbable(MqttProfile requiredUplink);
bool GatewayCandidateMatchesAuthenticated(const char *gatewayId);
// Reinstalls the gateway readiness filters on the newly active transport.
void GatewayCandidateOnConnected();
const char *GatewayNetworkTopicFilter();
const char *GatewayStatusTopicFilter();
}

#endif
