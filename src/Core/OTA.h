#pragma once
#ifndef NIGHTMARE_OTA_H
#define NIGHTMARE_OTA_H

#include <Modules.config.h>
#ifdef COMPILE_OTA
#include <ArduinoOTA.h>
#include <Core/Timers.h>

enum OTA_INFO
{
    OTA_START,
    OTA_END,
    OTA_ERROR,
    OTA_PROGRESS,
};
typedef void (*ota_callback_t)(OTA_INFO info, int data);
void initOTA();
void onOTAData(ota_callback_t callback);

#endif
#endif