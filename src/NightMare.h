#pragma once

#include <NightMare/Features.h>
#include <NightMare/HardwareProfile.h>
#include <Core/NightMareTypes.h>
#include <Core/DeviceIdentity.h>
#include <Core/RuntimeState.h>
#include <Core/SystemState.h>
#include <Core/Time.h>
#include <Core/NetResources.h>
#include <Core/Logs.h>
#include <Plataform/ESP32/NightMareESP.h>

#if NM_ENABLE_SETTINGS
#include <Core/StateStore.h>
#endif
#if NM_ENABLE_RESOURCES
#include <Core/ResourcesManager.h>
#endif
#if NM_ENABLE_CONSOLE
#include <Core/NightMareCommand.h>
#endif
#if NM_ENABLE_SCHEDULER
#include <Core/Scheduler.h>
#endif
#if NM_ENABLE_TELEMETRY
#include <Core/Telemetry.h>
#endif
#if NM_ENABLE_WIFI
#include <Plataform/ESP32/NightMareWIFI.h>
#endif
#if NM_ENABLE_MQTT
#include <Network/MQTT.h>
#endif
#if NM_ENABLE_TIME_SYNC
#include <Util/TimeSyncronization.h>
#endif
#if NM_ENABLE_OTA
#include <Util/OTA.h>
#endif
#if NM_ENABLE_HTTP
#include <HTTP/http.h>
#endif
#if NM_ENABLE_WEBSOCKET
#include <HTTP/websockets.h>
#endif
#if NM_ENABLE_LVGL
#include <Util/LVGL_Util.h>
#endif

static const char MattediWorksPresents[] PROGMEM = "\r\n\r\n\033[1;97mMattedi\033[0m\033[38;5;208mWorks\033[0m \033[3mpresents:\033[0m";

static const char NightMareNetworkFiglet[] PROGMEM = R"FIG(

 _   _ _       _     _   __  __                _   _      _                      _    
| \ | (_) __ _| |__ | |_|  \/  | __ _ _ __ ___| \ | | ___| |___      _____  _ __| | __
|  \| | |/ _` | '_ \| __| |\/| |/ _` | '__/ _ \  \| |/ _ \ __\ \ /\ / / _ \| '__| |/ /
| |\  | | (_| | | | | |_| |  | | (_| | | |  __/ |\  |  __/ |_ \ V  V / (_) | |  |   < 
|_| \_|_|\__, |_| |_|\__|_|  |_|\__,_|_|  \___|_| \_|\___|\__| \_/\_/ \___/|_|  |_|\_\
         |___/                                                                            

)FIG";
