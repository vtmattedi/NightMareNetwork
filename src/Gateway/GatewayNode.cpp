#include <Gateway/NightMareGateway.h>
#if NM_ENABLE_GATEWAY
#include <Gateway/GatewayNode.h>

#include <Gateway/Broker/Topic.h>
#include <Core/DeviceIdentity.h>
#include <Core/NightMareCommand.h>
#include <Core/ResourcesManager.h>
#include <Plataform/ESP32/NightMareESP.h>
#include <string>


namespace
{
// A Resource publication leaves like any gateway-originated message: to every
// matching ESP-NOW subscriber and to the MQTT broker. A retained one is kept in
// the vault, which replays it to the broker on every (re)connect and to every
// device that subscribes later, so it is accepted even with nobody listening
// yet; the ResourcesManager does not need to retry it.
class GatewayPublisher : public ResourcePublisher
{
public:
    bool publish(const std::string &topic, const std::string &payload, bool retained) override
    {
        NightMare::Message message;
        message.topic = topic;
        message.payload.assign(payload.begin(), payload.end());
        message.persistent = retained;
        message.direction = NightMare::Direction::LOCAL_TO_REMOTE;
        const bool delivered = publishToAll(message);
        return retained || delivered;
    }
};

// The gateway already sees every message: the MQTT client is subscribed to
// "#" and every ESP-NOW publish arrives here. handleMessage() picks out its own.
class GatewaySubscriber : public ResourceSubscriber
{
public:
    bool subscribe(const std::string &) override { return true; }
    bool unsubscribe(const std::string &) override { return true; }
};

GatewayPublisher s_publisher;
GatewaySubscriber s_subscriber;
bool s_started = false;

std::string text(const NightMare::Message &message)
{
    return std::string(message.payload.begin(), message.payload.end());
}

bool startsWith(const std::string &value, const std::string &prefix)
{
    return value.compare(0, prefix.size(), prefix) == 0;
}

NightMare::Message consoleAnswer(const NightMareResults &result)
{
    NightMare::Message answer;
    answer.payload.assign(result.response.begin(), result.response.end());
    return answer;
}

// The console a NightMare device answers on: <device>/console/in and the
// id-addressed <device>/console/controlled/<id>/in, each answered on the
// matching /out. The broadcast all/console/in is answered on the gateway's own
// <device>/console/out and still routed on, since every device answers it.
bool handleConsole(const NightMare::Message &message)
{
    const std::string &name = gDeviceIdentity.getDeviceName();
    const bool own = message.topic == name + "/console/in" ||
                     topicMatchesPattern(message.topic, name + "/console/controlled/+/in");
    const bool broadcast = message.topic == "all/console/in";
    if (!own && !broadcast)
        return false;

    const NightMareResults result = handleNightMareCommand(text(message));
    NightMare::Message answer = consoleAnswer(result);
    if (own)
    {
        replyMessage(message, answer);
        return true;
    }
    answer.topic = name + "/console/out";
    answer.direction = NightMare::Direction::LOCAL_TO_REMOTE;
    respondTo(message, answer);
    return false;
}
}

namespace GatewayNode
{
void begin()
{
    if (s_started)
        return;
    // Before startNightMareESP(): binding announces each Resource.
    gResourcesManager.setPublisher(&s_publisher);
    gResourcesManager.setSubscriber(&s_subscriber);
    startNightMareESP();
    s_started = true;
}

void tick()
{
    tickNightMareESP();
}

bool handleMessage(const NightMare::Message &message)
{
    if (!s_started)
        return false;
    if (handleConsole(message))
        return true;
    if (!gDeviceIdentity.isDevice(message.topic))
        return false;

    if (gResourcesManager.handleIngressMessage(message.topic, text(message), message.persistent))
        return true;

    // Over MQTT the broker sends the gateway its own publications back (it is
    // subscribed to "#"). Resource ones already reached local subscribers and
    // the vault; routing the echo again would deliver them twice and record
    // them as coming from the broker. Gateway presence (<id>/status,
    // <id>/gateway/...) is not caught here: local devices receive it this way.
    const std::string &name = gDeviceIdentity.getDeviceName();
    return message.direction == NightMare::Direction::REMOTE_TO_LOCAL &&
           (startsWith(message.topic, name + "/resource/") || startsWith(message.topic, name + "/manifest/"));
}
}

#endif // NM_ENABLE_GATEWAY
