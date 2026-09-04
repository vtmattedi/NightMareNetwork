#include "NightMareAsyncCommands.h"

#ifdef COMPILE_ASYNC_COMMANDS
#include "NightMareCommand.h"
#ifdef COMPILE_MQTT
#include <Core/MQTT.h>
#endif

#define COMPILE_SERIAL
#ifdef COMPILE_SERIAL
#define ASYNC_WORKER_LOGF(fmt, ...) Serial.printf("%s " fmt "\n", ASYNC_TAG, ##__VA_ARGS__)
#define ASYNC_WORKER_ERRORF(fmt, ...) Serial.printf("%s %s " fmt "\n", ERR_TAG, ASYNC_TAG, ##__VA_ARGS__)
#else
#define ASYNC_WORKER_LOGF(fmt, ...)
#define ASYNC_WORKER_ERRORF(fmt, ...)
#endif

static QueueHandle_t asyncCommandQueue = nullptr;
static bool asyncWorkerTaskRunning = false;

/// @brief Delivers a finished async command's result back to whatever issued it.
static void deliverAsyncResult(const String &command, const NightMareResults &res, const NightmareContext &context)
{
    if (context.msgSource == NM_CMD_SRC_MQTT && context.sourceIdentifier.length() > 0)
    {
#ifdef COMPILE_MQTT
        MQTT_Queue_Async_Message(context.sourceIdentifier, res.response, false, false);
        MQTT_Queue_Async_Message(context.sourceIdentifier, ASYNC_COMMAND_END_TAG, false, false);
#endif
    }
    else if (context.msgSource == NM_CMD_SRC_SERIAL && context.userContext)
    {
        SERIALTYPE *_Serial = reinterpret_cast<SERIALTYPE *>(context.userContext);
        _Serial->printf("<\x1b[90m%s\x1b[0m>%s\n", command.c_str(), OK_LOG(res.result));
        _Serial->printf("%s\n", res.response.c_str());
    }
}

static void xCommandWorkerTask(void *param)
{
    NightMareAsyncParam *taskParam;
    for (;;)
    {
        if (xQueueReceive(asyncCommandQueue, &taskParam, portMAX_DELAY) == pdPASS)
        {
            String command = taskParam->command;
            NightmareContext context = taskParam->context;
            delete taskParam;

            ASYNC_WORKER_LOGF("[%s] starting execution.", context.sourceIdentifier.c_str());
            unsigned long startTime = millis();
            NightMareResults res = executeNightMareCommand(command, context);
            ASYNC_WORKER_LOGF("[%s] executed in %lums.", context.sourceIdentifier.c_str(), millis() - startTime);

            if (res.context.msgSource != NM_CMD_ANS_DO_NOT_RESPOND)
                deliverAsyncResult(command, res, context);
        }
    }
}

bool startAsyncCommandWorker()
{
    if (asyncWorkerTaskRunning)
        return true;

    asyncCommandQueue = xQueueCreate(ASYNC_COMMANDS_QUEUE_SIZE, sizeof(NightMareAsyncParam *));
    if (asyncCommandQueue == NULL)
    {
        ASYNC_WORKER_ERRORF("Failed to create async command queue.");
        return false;
    }
    BaseType_t res = xTaskCreate(
        xCommandWorkerTask,
        "AsyncCmdWorker",
        ASYNC_COMMANDS_TASK_STACK,
        nullptr,
        ASYNC_COMMANDS_TASK_PRIORITY,
        nullptr);
    if (res != pdPASS)
    {
        ASYNC_WORKER_ERRORF("Error creating async handler task.");
        vQueueDelete(asyncCommandQueue);
        asyncCommandQueue = nullptr;
        return false;
    }
    ASYNC_WORKER_LOGF("%s Async handler Task Created.", OK_TAG);
    asyncWorkerTaskRunning = true;
    return true;
}

bool isAsyncCommandSystemReady()
{
    return asyncWorkerTaskRunning;
}

uint8_t dispatchAsyncCommand(String command, NightmareContext context)
{
    if (!asyncWorkerTaskRunning && !startAsyncCommandWorker())
        return ASYNC_CMD_SINGLE_TASK_NOT_INIT;

    NightMareAsyncParam *param = new NightMareAsyncParam();
    if (!param)
        return ASYNC_CMD_FAILED_TO_MALLOC_PARAMS;

    param->command = command;
    param->context = context;
    param->context.async = true; // Mark the context as async so handlers can know to respond with async message format if needed.

    if (xQueueSend(asyncCommandQueue, &param, 0) != pdPASS)
    {
        delete param;
        ASYNC_WORKER_ERRORF("Async command queue is full. Failed to dispatch command.");
        return ASYNC_CMD_QUEUE_FULL;
    }
    ASYNC_WORKER_LOGF("Dispatched async command to worker task: %s", command.c_str());
    return ASYNC_CMD_SUCCESS;
}

void asyncSend(const String &msg, NightmareContext context)
{
    // Used by commands to send messages asynchronously, e.g. progress updates, back to the source
    // of the command (MQTT topic, Serial port, etc.) while still running.
    if (context.msgSource == NM_CMD_SRC_MQTT)
    {
#ifdef COMPILE_MQTT
        MQTT_Send(context.sourceIdentifier, msg, false, false);
#endif
    }
    else if (context.msgSource == NM_CMD_SRC_SERIAL && context.userContext)
    {
        SERIALTYPE *_Serial = reinterpret_cast<SERIALTYPE *>(context.userContext);
        _Serial->println(msg);
    }
}

const char *asyncCommandErrorMessage(uint8_t code)
{
    switch (code)
    {
    case ASYNC_CMD_SUCCESS:
        return "Success.";
    case ASYNC_CMD_QUEUE_FULL:
        return "Async command queue is full. Please try again later.";
    case ASYNC_CMD_TASK_CREATION_FAILED:
        return "Failed to create task for async command.";
    case ASYNC_CMD_SINGLE_TASK_NOT_INIT:
        return "Async command worker task is not running.";
    case ASYNC_CMD_FAILED_TO_MALLOC_PARAMS:
        return "Failed to allocate memory for async command.";
    default:
        return "Unknown error dispatching async command.";
    }
}

#endif
