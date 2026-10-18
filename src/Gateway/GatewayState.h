#pragma once

#include <string>
#include <vector>
#include <Gateway/EspNow/espBroker.h>

namespace GatewayState
{
void begin();
// Which broker the MQTT uplink reaches, for the announcement's capabilities.
void setUplink(bool enabled, bool remote);
const std::string &id();
const std::string &generation();
std::string statusTopic();
std::string networkTopic();
std::string clientsTopic();
std::string statusJson(bool online);
std::string networkJson(bool online);
std::string clientsJson(const std::vector<EspBrokerDeviceInfo> &devices);
}
