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
#ifdef COMPILE_MQTT
#include <Core/MQTT.h>
#endif
#ifdef COMPILE_WIFI_MODULE
#include <Core/bWIFI.h>
#endif

#endif



void setCommandResolver(NightMareResults (*resolver)(const NightMareMessage &message));
NightMareResults handleNightMareCommand(const String &message);

#ifdef COMPILE_SERIAL_COMMAND_RESOLVER
void NightMareCommand_SerialResolver(char readUntilChar = '\n');
#endif