#pragma once
#include <NightMare/Features.h>
#include <NightMare/Core/Time.h>
#include <NightMare/Core/DeviceIdentity.h>
#include <NightMare/Resources/NetValue.h>
#include <NightMare/Resources/NetAction.h>
#include <NightMare/Resources/NetEvent.h>
#include <NightMare/Resources/ResourceManager.h>
#include <NightMare/Network/Network.h>
#include <NightMare/Runtime/Runtime.h>
#include <NightMare/Platform/Esp32SystemInfo.h>
#include <NightMare/Platform/Esp32Device.h>
#if NIGHTMARE_ENABLE_SETTINGS
#include <NightMare/Storage/SettingsStore.h>
#include <NightMare/Platform/Esp32SettingsPersistence.h>
#endif
#if NIGHTMARE_ENABLE_CONSOLE
#include <NightMare/Network/CommandParser.h>
#include <NightMare/Network/CommandRouter.h>
#include <NightMare/Network/Console.h>
#endif
#if NIGHTMARE_ENABLE_MQTT
#include <NightMare/Network/MqttTransport.h>
#endif
#if NIGHTMARE_ENABLE_TELEMETRY
#include <NightMare/Services/TelemetryService.h>
#endif
#if NIGHTMARE_ENABLE_OTA
#include <NightMare/Platform/OtaService.h>
#endif
#if NIGHTMARE_ENABLE_WIFI
#include <NightMare/Platform/WifiStation.h>
#endif
