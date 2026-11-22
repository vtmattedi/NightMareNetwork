#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_NETWORK
#include <Core/Text.h>
#include <Core/PlatformClock.h>

// Automatic NightMare routes, separate from the ESP MQTT client and project hook.
namespace NmMessageRouter
{
void onConnected();
// True when a route or the application handler took the message.
// frameworkConsumed, when given, says whether a framework route took it.
bool handleMessage(const std::string &topic, const std::string &payload, bool retained = false,
                   bool *frameworkConsumed = nullptr);
}
#endif // NM_ENABLE_NETWORK
