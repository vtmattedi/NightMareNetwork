#include "ota.h"
#ifdef COMPILE_OTA
#define COMPILE_SERIAL
#ifdef COMPILE_SERIAL
#define OTA_LOGF(fmt, ...) Serial.printf("%s " fmt "\n", "[OTA]", ##__VA_ARGS__)
#define OTA_ERRORF(fmt, ...) Serial.printf("%s %s " fmt "\n", ERR_LOG, "[OTA]", ##__VA_ARGS__)
#else
#define OTA_LOGF(fmt, ...)
#define OTA_ERRORF(fmt, ...)
#endif

ota_callback_t ota_user_callback = nullptr;
xTaskHandle otaTaskHandle = NULL;

void OTA_EventHandler(OTA_INFO info, int data = -1)
{
    if (ota_user_callback)
    {
        ota_user_callback(info, data);
    }
}

void startOTA()
{
    OTA_LOGF("Start updating %s", ArduinoOTA.getCommand() == 0 ? "flash" : "filesystem");
    OTA_EventHandler(OTA_START, ArduinoOTA.getCommand());
}

void endOTA()
{
    OTA_LOGF("Ended");
    OTA_EventHandler(OTA_END);
}

void progressOTA(unsigned int progress, unsigned int total)
{
    // update_progress = (float)progress / total * 100;
    OTA_LOGF("Progress: %u%%", (progress / (total / 100)));
    OTA_EventHandler(OTA_PROGRESS, (progress / (total / 100)));
}

void errorOTA(ota_error_t error)
{
#ifdef COMPILE_SERIAL
    OTA_ERRORF("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
        OTA_ERRORF("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
        OTA_ERRORF("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
        OTA_ERRORF("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
        OTA_ERRORF("Receive Failed");
    else if (error == OTA_END_ERROR)
        OTA_ERRORF("End Failed");
#endif

    OTA_EventHandler(OTA_ERROR, error);
}

void otaTask(void *param)
{
    while (true)
    {
        ArduinoOTA.handle();
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}

void initOTA()
{
    ArduinoOTA.setHostname(DEVICE_NAME);
    ArduinoOTA.onStart(startOTA);
    ArduinoOTA.onEnd(endOTA);
    ArduinoOTA.onProgress(progressOTA);
    ArduinoOTA.onError(errorOTA);
    ArduinoOTA.begin();
    // bool res = Timers.create("OTA", 1, []()
    //                     { ArduinoOTA.handle(); }, true);
    bool res = xTaskCreate(
        otaTask,
        "OTA_Task",
        4096,
        NULL,
        10,
        &otaTaskHandle);

    // Serial.printf("%s OTA Initialized\n", OK_LOG(res));
}

void onOTAData(ota_callback_t callback)
{
    ota_user_callback = callback;
}

#endif