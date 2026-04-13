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
#ifdef COMPILE_HTTP_SERVER
#include <HTTP/http.h>
#endif
#ifdef COMPILE_WEBSOCKET_SERVER
#include <HTTP/websockets.h>
#endif
#ifdef COMPILE_CONFIGS
#include <Core/Configs.h>
#endif

#ifdef COMPILE_TIMERS
#include <Core/Timers.h> 
#endif

#endif


void setCommandResolver(NightMareResults (*resolver)(const NightMareMessage &message));

#ifdef COMPILE_ASYNC_COMMANDS
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#define ASYNC_COMMANDS_TASK_STACK 4096
#define ASYNC_COMMANDS_TASK_PRIORITY 1
#define ASYNC_COMMANDS_QUEUE_SIZE 10
#define ASYNC_COMMANDS_SINGLE_TASK_DELAY_MS 10
#define ASYNC_COMMAND_END_TAG ";;finished;;"
#define ASYNC_COMMAND_ERROR_TAG(var) String(String(";;error;;") + String(var) + String(";;")).c_str()
enum AsyncCommandResult
{
    ASYNC_CMD_SUCCESS = 0,
    ASYNC_CMD_QUEUE_FULL = 1,
    ASYNC_CMD_TASK_CREATION_FAILED = 2,
    ASYNC_CMD_SINGLE_TASK_NOT_INIT = 3,
    ASYNC_CMD_FAILED_TO_MALLOC_PARAMS = 4
};


uint8_t dispatchAsyncCommand(String command, NightmareContext context);

void asyncSend(const String &msg, NightmareContext context);

#endif

#ifdef COMPILE_SERIAL_COMMAND_RESOLVER
void NightMareCommand_SerialResolver(HardwareSerial* _Serial, char readUntilChar = '\n');
#endif