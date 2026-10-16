#pragma once
#include <NightMare/Features.h>
#if NM_ENABLE_CONSOLE
#include <Core/NightMareTypes.h>
#include <Core/StateStore.h>
#include <Core/SystemState.h>

#define DELIMITER (char)' '

#if NM_CONSOLE_BUILTINS
#include <ArduinoJson.h>

#if NM_ENABLE_NETWORK
#include <Network/NmConnection.h>
#endif
#if NM_ENABLE_WIFI
#include <Network/WiFiIP/NmWifiService.h>
#endif
#if NM_ENABLE_HTTP
#include <HTTP/http.h>
#endif
#if NM_ENABLE_WEBSOCKET
#include <HTTP/websockets.h>
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
NightMareMessage parseNightMareMessage(const std::string &message);

/// @brief Checks a string against a length limit, saying by how much it overran. Const, so it can
/// be pointed straight at an incoming payload: MQTT, HTTP, WS and TCP hand the parser an unbounded
/// buffer they never sized themselves.
/// @param str The string to check.
/// @param maxLength The limit to enforce, e.g. NM_MAX_MESSAGE_LEN.
/// @param error Set to the reason when this returns false; left alone on success.
/// @return True when `str` is within the limit, false when it overran.
bool ensureSize(const std::string &str, size_t maxLength, std::string &error);

/// @brief Parses a raw command line into command / subcommand / args, reporting malformed input
/// instead of guessing at it. Collapses runs of any whitespace, supports `\` escapes, and treats
/// `""` as a present-but-empty argument; `argc` gives the number of arguments actually supplied.
/// Check `valid` before using the result.
/// @param message The command string to parse.
/// @return The parsed message, or one with `valid` false and `error` set.
NightMareMessage parseNightMareMessage2(const std::string &message);

/// @brief Parses and executes a command on the calling task. With resources enabled, `>list`
/// lists resources, `>manifest json` returns the local manifest (MessagePack republishes),
/// `>drop name` unbinds a resource,
/// `>raw topic payload` enters MQTT ingress, and `> name [get|set|invoke] [payload]` addresses
/// a resource by its unique name.
/// `CONFIG list|get|set|manifest` is routed directly to configManager() and
/// preserves the Config payload as one opaque std::string.
/// @param message The raw command string, e.g. `"WIFI SCAN -s"`. Callers that only have a message
/// string can omit context entirely, e.g. `handleNightMareCommand("PING")`.
/// @param context Execution context; defaults to an anonymous synchronous context.
/// @return The command result.
NightMareResults handleNightMareCommand(const std::string &message, NightmareContext context = NightmareContext());

#if NM_CONSOLE_SERIAL
/// @brief Reads pending console bytes from ESP-IDF standard input and resolves complete lines.
void NightMareCommand_SerialResolver();
#endif
#endif // NM_ENABLE_CONSOLE
