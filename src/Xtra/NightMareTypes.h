#pragma once
#include <Arduino.h>
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