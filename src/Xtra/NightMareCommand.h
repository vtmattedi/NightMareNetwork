#pragma once
#include <Modules.config.h>
#include <Arduino.h>

#define DELIMITER (char)' '

#ifdef ENABLE_PREPROCESSING
#ifdef SCHEDULER_AWARE
#include <Xtra/Scheduler.h>
#endif
#endif

struct NightMareResults
{
    bool result;
    String response;
};

struct NightMareMessage
{
    String command;
    String args[5] = {"", "", "", "", ""};
};

void setCommandResolver(NightMareResults (*resolver)(const NightMareMessage &message));
NightMareResults handleNightMareCommand(const String &message);