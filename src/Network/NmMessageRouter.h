#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_MQTT
#include <Arduino.h>

// Automatic NightMare routes, separate from the ESP MQTT client and project hook.
namespace NmMessageRouter
{
void onConnected();
bool handleMessage(const String &topic, const String &payload);
}
#endif // NM_ENABLE_MQTT
