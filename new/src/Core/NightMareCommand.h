#pragma once
#include <Core/NightMareTypes.h>
#include <Core/StateStore.h>

#define DELIMITER (char)' '

#ifdef ENABLE_PREPROCESSING
#include <TimeLib.h>
#include <ArduinoJson.h>

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
#ifdef COMPILE_MISC
#include <Core/Misc.h>
#endif
#ifdef COMPILE_SYSTEMSTATUS
#include <Core/SystemStatus.h>
#endif

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

/// @brief Parses and executes a command on the calling task.
/// @param message The raw command string, e.g. `"WIFI SCAN -s"`. Callers that only have a message
/// string can omit context entirely, e.g. `handleNightMareCommand("PING")`.
/// @param context Execution context; defaults to an anonymous synchronous context.
/// @return The command result.
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
