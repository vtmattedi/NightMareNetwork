#pragma once

// #include <ArduinoOTA.h>
#include <ArduinoOTA.h>
#include <Core/StateStore.h>
#define OTA_TIMEOUT_MS 5000
#define OTA_TASK_PRIORITY 1

enum OTA_INFO
{
    OTA_START,
    OTA_END,
    OTA_ERROR,
    OTA_PROGRESS,
};

typedef void (*ota_callback_t)(OTA_INFO info, int data);
void initOTA();
void onOTAEvent(ota_callback_t callback);

