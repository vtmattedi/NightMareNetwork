#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_NETWORK
#include <Core/Text.h>
#include <Core/PlatformClock.h>

// Automatic NightMare routes, separate from the ESP MQTT client and project hook.
namespace NmMessageRouter
{
void onConnected();
bool handleMessage(const std::string &topic, const std::string &payload, bool retained = false);
}
#endif // NM_ENABLE_NETWORK
