#include "OTA.h"

ota_callback_t ota_user_callback = nullptr;
TaskHandle_t otaTaskHandle = NULL;

void OTA_EventHandler(OTA_INFO info, int data = -1)
{
    if (ota_user_callback)
    {
        ota_user_callback(info, data);
    }
}

void startOTA()
{
    SystemState.setFlag("ota_running", true);
    OTA_EventHandler(OTA_START, ArduinoOTA.getCommand());
}

void endOTA()
{
    SystemState.setFlag("ota_running", false);
    OTA_EventHandler(OTA_END);
}

void progressOTA(unsigned int progress, unsigned int total)
{
    OTA_EventHandler(OTA_PROGRESS, (progress / (total / 100)));
}

void errorOTA(ota_error_t error)
{
    SystemState.setFlag("ota_running", false);

    OTA_EventHandler(OTA_ERROR, error);
}

void otaTask(void *param)
{
    while (true)
    {
        ArduinoOTA.handle();
        int taskDealay = SystemState.getFlag("ota_running") ? 5 : 1000;
        vTaskDelay(taskDealay / portTICK_PERIOD_MS);
        yield();
    }
    // should never reach here, but if it does, we should clean up and disable OTA
    SystemState.setFlag("ota_enabled", false);
}

void initOTA()
{

    ArduinoOTA.setHostname(getDeviceName());
    ArduinoOTA.onStart(startOTA);
    ArduinoOTA.onEnd(endOTA);
    ArduinoOTA.onProgress(progressOTA);
    ArduinoOTA.onError(errorOTA);
    ArduinoOTA.setTimeout(OTA_TIMEOUT_MS);
    ArduinoOTA.begin();
    bool res = xTaskCreate(
        otaTask,
        "OTA_Task",
        4096,
        NULL,
        OTA_TASK_PRIORITY,
        &otaTaskHandle);
    SystemState.setFlag("ota_enabled", res);
}

void onOTAEvent(ota_callback_t callback)
{
    ota_user_callback = callback;
}

