---
title: Command resolver
description: The parser, the two-stage resolver, the execution context that says where a command came from, the serial resolver, and async commands.
section: modules
order: 110
---

# Command resolver — `Xtra/NightMareCommand.h`

Everything a command goes through between arriving as text and answering. The
grammar and the built-in command list are in
[Commands](/docs/protocols/commands); this page is the API. Compiled with
`COMPILE_COMMAND_RESOLVER`, with `ENABLE_PREPROCESSING` for the built-ins and
`COMPILE_SERIAL_COMMAND_RESOLVER` for the serial front end.

## Types — `Xtra/NightMareTypes.h`

```cpp
struct NightMareMessage {
    String  command;        // uppercased first token
    String  subcommand;     // uppercased copy of args[0]
    String  args[5];        // NM_MAX_ARGS; a command's own parameters start at args[1]
    uint8_t argc;           // how many were actually supplied -- "absent" vs "empty"
    bool    valid;          // false when the line could not be parsed
    String  error;          // why
};

struct NightmareContext {
    CommandSource msgSource;     // NM_CMD_SRC_MQTT / SERIAL / HTTP / WEBSOCKET / SCHEDULER, or NM_CMD_ANS_DO_NOT_RESPOND
    String        sourceIdentifier;  // MQTT reply topic, etc.
    void         *userContext;   // e.g. the Serial or client object to answer on
    bool          async;         // queue for the async worker instead of running inline
};

struct NightMareResults {
    bool            result;
    String          response;
    NightmareContext context;
};
```

`NM_MAX_MESSAGE_LEN` (512) is enforced in the parser, because MQTT, HTTP,
WebSocket and TCP all hand it an unbounded payload.

## Functions

```cpp
void setCommandResolver(NightMareResults (*resolver)(const NightMareMessage &));

NightMareMessage parseNightMareMessage2(const String &line);   // the parser; check .valid
NightMareMessage parseNightMareMessage(const String &line);    // legacy, no error reporting
bool             ensureSize(const String &s, size_t max, String &error);

NightMareResults handleNightMareCommand(const String &line, NightmareContext ctx = NightmareContext());
NightMareResults executeNightMareCommand(const String &line, NightmareContext ctx);   // always synchronous

void NightMareCommand_SerialResolver(SERIALTYPE *serial, char readUntil = '\n');    // from loop()
```

`handleNightMareCommand()` is the entry point every transport uses: it parses,
runs the built-in preprocessor, and falls through to the registered resolver.
If `ctx.async` is set it queues the command instead and returns at once; the
worker delivers the real reply later through the context's source.
`executeNightMareCommand()` is the synchronous core, exposed so the async
worker can call it without re-queueing.

A parse failure becomes a `Parse error: <reason>` response rather than a
half-parsed line falling through as "unknown command".

## The serial resolver

```cpp
void loop() { NightMareCommand_SerialResolver(&Serial, '\n'); }
```

Reads a line at a time and runs it. `SERIALTYPE` is `HardwareSerial`, or
`HWCDC` when `ESP32_C3` is defined — the C3's console is native USB and the
two classes are unrelated, so the switch has to match the board.

## Async commands — `Xtra/NightMareAsyncCommands.h`

For commands that take longer than a caller should wait — a WiFi scan, a
long probe. Compiled with `COMPILE_ASYNC_COMMANDS`; `ASYNC_COMMANDS_SINGLE_TASK`
runs them all on one worker task (recommended) rather than a task per
command.

```cpp
uint8_t dispatchAsyncCommand(String command, NightmareContext ctx);   // ctx.async forced true
void    asyncSend(const String &msg, NightmareContext ctx);           // progress from inside a handler
bool    isAsyncCommandSystemReady();
```

The worker (`ASYNC_COMMANDS_TASK_STACK` 8192, priority 2, queue of 10)
replies through the context's source as the command produces output, and
terminates with `;;finished;;`; a failure is `;;error;;<message>;;`. A handler
that wants to stream progress calls `asyncSend()` with the context it was
given.

Telemetry reports `ASYNC_enabled`, so a consumer can tell whether a device
will honour an async request.

## Writing a resolver

```cpp
NightMareResults localHandleNightMareCommand(const NightMareMessage &m)
{
    NightMareResults res;
    if (m.command == "IR")
    {
        if (m.subcommand == "SEND") { ... res.result = irSendByName(m.args[1]); ... }
        else { res.result = false; res.response = "Unknown IR subcommand available: [SEND, LIST, INFO]."; }
        return res;
    }
    res.result = false;
    res.response = "Unknown command available: [IR, SENSORS, HELP].";
    return res;
}
```

Three habits that make a resolver pleasant from a terminal: every "unknown"
reply lists what would have been valid; state comes back as JSON; and a
command that changes something answers with the new state, so the caller
does not need a second round trip to confirm it.
