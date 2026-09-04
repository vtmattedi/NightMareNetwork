#pragma once
#include <Arduino.h>

enum CommandSource
{
    NM_CMD_SRC_UNKNOWN = 0,
    NM_CMD_SRC_MQTT = 1,
    NM_CMD_SRC_SERIAL = 2,
    NM_CMD_SRC_HTTP = 3,
    NM_CMD_SRC_WEBSOCKET = 4,
    NM_CMD_SRC_SCHEDULER = 5,
    NM_CMD_ANS_DO_NOT_RESPOND = 0xFF
};

/// @brief Struct to hold the context of a command, including its source, identifier, user-defined context, and whether it should be handled asynchronously.
/// @param msgSource The source of the command (e.g., MQTT, Serial, HTTP, Websocket, Scheduler).
/// @param sourceIdentifier A string identifier for the command source (e.g., MQTT topic,
/// @param userContext An optional pointer to user-defined context (e.g., client object).
/// @param async A boolean indicating whether the command should be handled asynchronously.
struct NightmareContext
{
    CommandSource msgSource; // 0 = unknown, 1 = MQTT, 2 = Serial, 3 = HTTP, 4 = Websocket, 0xFF = Do not respond
    String sourceIdentifier; // e.g. MQTT topic, Serial port, HTTP endpoint, Websocket ID
    void *userContext;       // Optional pointer for user-defined context (e.g. client object)
    bool async;              // Whether the command should be handled asynchronously (only applicable for MQTT commands, ignored for other sources)

    NightmareContext(CommandSource source = NM_CMD_SRC_UNKNOWN,
                     const String &identifier = "",
                     void *ctx = nullptr,
                     bool isAsync = false)
        : msgSource(source), sourceIdentifier(identifier), userContext(ctx), async(isAsync) {}
};

struct NightMareResults
{
    bool result;
    String response;
    NightmareContext context; // Optional pointer for user-defined context (e.g. client object)
};

struct NightMareMessage
{
    String command;
    String subcommand;
    String args[5] = {"", "", "", "", ""};
};

struct NightMareAsyncParam
{
    String command;
    NightmareContext context;
};