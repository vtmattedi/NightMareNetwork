#include <Gateway/NightMareGateway.h>
#if NM_ENABLE_GATEWAY
#include <Gateway/GatewayNode.h>

#include <Core/DeviceIdentity.h>
#include <Network/NmConnectionInternal.h>
#include <Plataform/ESP32/NightMareESP.h>
#include <atomic>
#include <string>

namespace
{
std::atomic<bool> s_started{false};

std::string text(const NightMare::Message &message)
{
    return std::string(message.payload.begin(), message.payload.end());
}

bool startsWith(const std::string &value, const std::string &prefix)
{
    return value.compare(0, prefix.size(), prefix) == 0;
}

// What the broker sends back of the gateway's own publications (it is
// subscribed to "#"). They already reached local subscribers and the vault;
// routing the echo again would deliver them twice and record them as coming
// from the broker. Presence (<id>/status) and the gateway documents
// (<id>/gateway/...) are not echoes to drop: local devices receive them this
// way.
bool ownEcho(const NightMare::Message &message)
{
    if (message.direction != NightMare::Direction::REMOTE_TO_LOCAL)
        return false;
    std::string relative;
    if (!gDeviceIdentity.relativeTopic(message.topic, relative))
        return false;
    return relative != "status" && !startsWith(relative, "gateway/") &&
           !startsWith(relative, "console/controlled/") && relative != "console/in";
}
}

namespace GatewayNode
{
void begin()
{
    if (s_started.load(std::memory_order_acquire))
        return;
    s_started.store(true, std::memory_order_release);
    // ConnectionBegin(), inside, finds the GATEWAY connection up and announces
    // the device: status, manifests, states, info, telemetry.
    startNightMareESP();
}

bool started()
{
    return s_started.load(std::memory_order_acquire);
}

void tick()
{
    tickNightMareESP();
}

bool publish(const char *topic, const uint8_t *payload, size_t length, bool retained)
{
    NightMare::Message message;
    message.topic = topic;
    if (length != 0)
        message.payload.assign(payload, payload + length);
    message.persistent = retained;
    message.direction = NightMare::Direction::LOCAL_TO_REMOTE;
    const bool delivered = publishToAll(message);
    return retained || delivered;
}

bool handleMessage(const NightMare::Message &message)
{
    if (!started())
        return false;
    bool consumed = false;
    NightMare::OnGatewayIngress(message.topic, text(message), message.persistent, consumed);
    // A request to the gateway itself ends here. The broadcast console and
    // Control/time were acted on too but belong to every device: routed on.
    if (consumed && gDeviceIdentity.isDevice(message.topic))
        return true;
    return ownEcho(message);
}
}

#endif // NM_ENABLE_GATEWAY
