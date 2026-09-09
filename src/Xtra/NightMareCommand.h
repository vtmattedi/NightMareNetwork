#pragma once
#include <Modules.config.h>
#include <Xtra/NightMareTypes.h>

#define DELIMITER (char)' '

#ifdef ENABLE_PREPROCESSING
#include <TimeLib.h>
#include <ArduinoJson.h>

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
#ifdef COMPILE_MISC
#include <Core/Misc.h>
#endif
#ifdef COMPILE_SYSTEMSTATUS
#include <Core/SystemStatus.h>
#endif
#ifdef COMPILE_TIMERS
#include <Core/Timers.h>
#endif

#endif

#ifdef COMPILE_ASYNC_COMMANDS
#include <Xtra/NightMareAsyncCommands.h>
#endif

/// @brief Registers the user-supplied command handler invoked for any command the built-in
/// preprocessor doesn't already resolve.
/// @param resolver Function taking a `const NightMareMessage &` and returning a `NightMareResults`.
void setCommandResolver(NightMareResults (*resolver)(const NightMareMessage &message));

/// @brief Parses a raw command line into command / subcommand / args, per the NightMare Message
/// grammar: `COMMAND SUBCOMMAND ARG0 ARG1 ARG2 ARG3 ARG4`. Command and subcommand are uppercased;
/// args are kept as-is. A `"..."` quoted word is taken as a single argument with the quotes removed.
/// @param message The command string to parse.
/// @return The parsed NightMareMessage.
NightMareMessage parseNightMareMessage(const String &message);

/// @brief Checks a string against a length limit, saying by how much it overran. Const, so it can
/// be pointed straight at an incoming payload: MQTT, HTTP, WS and TCP hand the parser an unbounded
/// buffer they never sized themselves.
/// @param str The string to check.
/// @param maxLength The limit to enforce, e.g. NM_MAX_MESSAGE_LEN.
/// @param error Set to the reason when this returns false; left alone on success.
/// @return True when `str` is within the limit, false when it overran.
bool ensureSize(const String &str, size_t maxLength, String &error);

/// @brief Parses a raw command line into command / subcommand / args, reporting malformed input
/// instead of guessing at it. Collapses runs of any whitespace, supports `\` escapes, and treats
/// `""` as a present-but-empty argument; `argc` gives the number of arguments actually supplied.
/// Check `valid` before using the result.
/// @param message The command string to parse.
/// @return The parsed message, or one with `valid` false and `error` set.
NightMareMessage parseNightMareMessage2(const String &message);

/// @brief Core synchronous command executor: parses the message, runs it through the built-in
/// preprocessor, and falls back to the registered resolver. Always runs on the calling task.
/// Most callers want handleNightMareCommand() instead; this is exposed for the async worker to
/// invoke without re-triggering async dispatch.
/// @param message The input command message as a string.
/// @param context The context of the command, including its source and identifier.
/// @return A NightMareResults struct containing the result of the command execution.
NightMareResults executeNightMareCommand(const String &message, NightmareContext context);

/// @brief Single entrypoint for running a NightMare command. Runs synchronously unless
/// `context.async` is set, in which case the command is queued for the async worker (started
/// automatically on first use) and this returns immediately; the worker delivers the real response
/// later via the context's source. If the async dispatch itself fails (queue full, worker couldn't
/// start, ...), a failure result is returned rather than silently falling back to a blocking call.
/// @param message The raw command string, e.g. `"WIFI SCAN -s"`. Callers that only have a message
/// string can omit context entirely, e.g. `handleNightMareCommand("PING")`.
/// @param context Execution context; defaults to an anonymous synchronous context.
/// @return The command result, or the dispatch outcome when queued asynchronously.
NightMareResults handleNightMareCommand(const String &message, NightmareContext context = NightmareContext());

#ifdef COMPILE_SERIAL_COMMAND_RESOLVER
#ifdef ESP32_C3
#define SERIALTYPE HWCDC
#else
#define SERIALTYPE HardwareSerial
#endif

/// @brief Generic function to listen to Serial input and resolve commands using the NightMare command resolver.
/// @param _Serial Serial object pointer to the serial interface to listen to.
/// @param readUntilChar char to read until, default is '\n'. If set to 0, it will read until no more data is available in the buffer, allowing for multi-line commands.
void NightMareCommand_SerialResolver(SERIALTYPE *_Serial, char readUntilChar = '\n');
#endif
