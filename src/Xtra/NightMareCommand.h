#pragma once
#include <Modules.config.h>
#include <Xtra/NightMareTypes.h>

#define DELIMITER (char)' '

#ifdef ENABLE_PREPROCESSING
#include <TimeLib.h>
#include <ArduinoJson.h>
const char *getBootReason(int reason);

#ifdef SCHEDULER_AWARE
#include <Xtra/Scheduler.h>
#endif
#endif

#ifdef COMPILE_MQTT
#include <Core/MQTT.h>
#endif


void setCommandResolver(NightMareResults (*resolver)(const NightMareMessage &message));
NightMareResults handleNightMareCommand(const String &message);