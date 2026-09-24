#include <NightMare/Features.h>
#if NM_ENABLE_OTA
#include "OTA.h"
#include <Core/DeviceIdentity.h>

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
    SystemState.set(SystemFlag::OtaRunning);
    OTA_EventHandler(OTA_START, ArduinoOTA.getCommand());
}

void endOTA()
{
    SystemState.clear(SystemFlag::OtaRunning);
    OTA_EventHandler(OTA_END);
}

void progressOTA(unsigned int progress, unsigned int total)
{
    OTA_EventHandler(OTA_PROGRESS, (progress / (total / 100)));
}

void errorOTA(ota_error_t error)
{
    SystemState.clear(SystemFlag::OtaRunning);

    OTA_EventHandler(OTA_ERROR, error);
}

void otaTask(void *param)
{
    while (true)
    {
        ArduinoOTA.handle();
        int taskDealay = SystemState.get(SystemFlag::OtaRunning) ? 5 : 1000;
        vTaskDelay(taskDealay / portTICK_PERIOD_MS);
        yield();
    }
}

void initOTA()
{

    ArduinoOTA.setHostname(gDeviceIdentity.getDeviceName().c_str());
    ArduinoOTA.onStart(startOTA);
    ArduinoOTA.onEnd(endOTA);
    ArduinoOTA.onProgress(progressOTA);
    ArduinoOTA.onError(errorOTA);
    ArduinoOTA.setTimeout(OTA_TIMEOUT_MS);
    ArduinoOTA.begin();
    xTaskCreate(
        otaTask,
        "OTA_Task",
        4096,
        NULL,
        OTA_TASK_PRIORITY,
        &otaTaskHandle);
}

void onOTAEvent(ota_callback_t callback)
{
    ota_user_callback = callback;
}
#endif // NM_ENABLE_OTA
