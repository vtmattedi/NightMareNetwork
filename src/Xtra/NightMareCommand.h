#pragma once
#include <Arduino.h>
#define ENABLE_PREPROCESSING

#ifdef ENABLE_PREPROCESSING
#define SCHEDULER_AWARE
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