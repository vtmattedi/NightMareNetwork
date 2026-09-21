#pragma once
#include <Arduino.h>

enum CommandSource
{
    NM_CMD_SRC_UNKNOWN = 0,
    NM_CMD_SRC_MQTT = 1,
    NM_CMD_SRC_SERIAL = 2,
    NM_CMD_SRC_HTTP = 3,
    NM_CMD_SRC_WEBSOCKET = 4,
    NM_CMD_SRC_JOB = 5,
    NM_CMD_ANS_DO_NOT_RESPOND = 0xFF
};

/// @brief Struct to hold the context of a command, including its source, identifier, and user-defined context.
/// @param msgSource The source of the command (e.g., MQTT, Serial, HTTP, Websocket, Job).
/// @param sourceIdentifier A string identifier for the command source (e.g., MQTT topic,
/// @param userContext An optional pointer to user-defined context (e.g., client object).
struct NightmareContext
{
    CommandSource msgSource; // 0 = unknown, 1 = MQTT, 2 = Serial, 3 = HTTP, 4 = Websocket, 0xFF = Do not respond
    String sourceIdentifier; // e.g. MQTT topic, Serial port, HTTP endpoint, Websocket ID
    void *userContext;       // Optional pointer for user-defined context (e.g. client object)

    NightmareContext(CommandSource source = NM_CMD_SRC_UNKNOWN,
                     const String &identifier = "",
                     void *ctx = nullptr)
        : msgSource(source), sourceIdentifier(identifier), userContext(ctx) {}
};

struct NightMareResults
{
    bool result;
    String response;
    NightmareContext context; // Optional pointer for user-defined context (e.g. client object)
};

/// Arguments captured after the command word. Raising this is the only change needed to widen the
/// grammar; parseNightMareMessage2() derives its bounds from it.
#define NM_MAX_ARGS 5
/// Longest command line parseNightMareMessage2() will accept. MQTT, HTTP, WS and TCP hand the
/// parser an untrimmed, unbounded payload, so the cap is enforced here rather than at each caller.
#define NM_MAX_MESSAGE_LEN 512

struct NightMareMessage
{
    String command;
    String subcommand;
    String args[NM_MAX_ARGS];
    /// Number of arguments actually supplied, which is what separates "absent" from "empty string".
    /// Only set by parseNightMareMessage2(); the original parser leaves it at 0.
    uint8_t argc = 0;
    /// False when the input was malformed. Only set by parseNightMareMessage2(), which is why it
    /// defaults to true: a message from the original parser is always "valid" as far as callers go.
    bool valid = true;
    /// Why parsing failed, when valid is false.
    String error;
};

