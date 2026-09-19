#pragma once
#include <Modules.config.h>
#include <Xtra/NightMareTypes.h>

#ifdef COMPILE_ASYNC_COMMANDS
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define ASYNC_COMMANDS_TASK_STACK 8192
#define ASYNC_COMMANDS_TASK_PRIORITY 2
#define ASYNC_COMMANDS_QUEUE_SIZE 10
#define ASYNC_COMMAND_END_TAG ";;finished;;"
#define ASYNC_COMMAND_ERROR_TAG(var) String(String(";;error;;") + String(var) + String(";;")).c_str()

/// @brief Outcome of a dispatchAsyncCommand() call.
enum AsyncCommandResult
{
    ASYNC_CMD_SUCCESS = 0,
    ASYNC_CMD_QUEUE_FULL = 1,
    ASYNC_CMD_TASK_CREATION_FAILED = 2,
    ASYNC_CMD_SINGLE_TASK_NOT_INIT = 3,
    ASYNC_CMD_FAILED_TO_MALLOC_PARAMS = 4
};

/// @brief Starts the async command worker task and its queue if not already running.
/// Called automatically by dispatchAsyncCommand() on first use; safe to call more than once.
/// @return true once the worker is up and accepting commands.
bool startAsyncCommandWorker();

/// @brief True once the async worker task and queue are running and accepting commands.
bool isAsyncCommandSystemReady();

/// @brief Queues a command for execution on the async worker task, starting the worker if needed.
/// The response (if any) is delivered later by the worker via the context's source (e.g. MQTT topic,
/// Serial port), not returned here.
/// @param command The raw command string.
/// @param context Context to execute it with; its `async` flag is forced true.
/// @return An AsyncCommandResult status code.
uint8_t dispatchAsyncCommand(String command, NightmareContext context);

/// @brief Sends a message back to the source of a command, e.g. for progress/partial updates from
/// within a long-running command handler.
void asyncSend(const String &msg, NightmareContext context);

/// @brief Human-readable description for an AsyncCommandResult code, suitable for a failure response.
const char *asyncCommandErrorMessage(uint8_t code);

#endif
