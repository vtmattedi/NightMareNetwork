---
title: Commands
description: NightMare command grammar, transports, built-ins, and USER Scheduler jobs.
section: protocols
order: 40
---

# Commands

NightMare has one text command grammar that can be executed from different transports.

The parser and built-in command handler are shared. A command behaves as the same command whether it came from serial, MQTT console, controlled MQTT request/response, an HTTP endpoint, or a Scheduler String job.

## Grammar

The parser accepts:

```text
COMMAND [ARG0 [ARG1 [ARG2 [ARG3 [ARG4]]]]]
```

The conventional interpretation is:

```text
COMMAND SUBCOMMAND ARG1 ARG2 ARG3 ARG4
```

because `args[0]` is also exposed as `subcommand`.

Current limits:

```text
maximum command-line length: 512 characters
maximum arguments after command: 5
```

The command word and subcommand are converted to uppercase.

Remaining arguments preserve their case.

## Whitespace

Runs of whitespace outside quoted/literal regions are treated as separators.

For example:

```text
CONFIG    GET    myKey
```

parses like:

```text
CONFIG GET myKey
```

## Double quotes

Double quotes group whitespace into one argument and are removed.

Example:

```text
CONFIG SET greeting "hello world"
```

An explicitly empty argument is preserved:

```text
""
```

The parser reports an unterminated quote as an error instead of guessing.

## Backtick literals

Backticks create a verbatim argument region.

Example:

```text
JOB AFTER once 1000 `CONFIG SET message "hello world"`
```

The content inside the backtick fence is kept verbatim, including whitespace and quote characters.

A run of `N` opening backticks is closed only by a run of exactly `N` backticks. This permits literal backticks inside a fenced argument by choosing a longer fence.

## Backslash behavior

Backslash only escapes:

```text
"
`
```

before those delimiters.

Otherwise the backslash is preserved.

This lets filesystem-style text such as:

```text
C:\tmp
/a\b.json
```

round-trip without requiring every backslash to be doubled.

## Parse errors

Malformed commands return explicit parser errors for cases such as:

```text
empty command
too many arguments
unterminated quote
unterminated ` literal
message too long
```

They do not silently fall through as unknown commands.

## Transports

The same command handler is used by several sources.

### Serial

When:

```cpp
NM_CONSOLE_SERIAL
```

is enabled, `tickNightMareESP()` services the serial resolver.

### MQTT console

Request:

```text
<device>/console/in
```

Response:

```text
<device>/console/out
```

Broadcast request:

```text
all/console/in
```

Each receiving device responds on its own `<device>/console/out`.

### Controlled MQTT / MQTTP

Request:

```text
<device>/console/controlled/<id>/in
```

Response:

```text
<device>/console/controlled/<id>/out
```

See [MQTTP](mqttp.md).

### Scheduler

A String command job runs through the same `handleNightMareCommand()` path with command source `JOB`.

## Built-in precedence

When built-ins are enabled, built-in commands are handled before the project resolver.

An application command resolver registered with:

```cpp
setCommandResolver(...)
```

receives commands that the built-in preprocessor does not recognize.

Applications therefore extend the command set; they do not override an enabled built-in with the same name.

## Feature-dependent commands

Not every command exists in every build.

```text
JOB
    requires NM_ENABLE_JOBS

INFO
    requires NM_ENABLE_TELEMETRY

MQTT
    requires NM_ENABLE_MQTT

WIFI
    requires NM_ENABLE_WIFI

HTTPSERVER
    requires NM_ENABLE_HTTP

WS
    requires NM_ENABLE_WEBSOCKET

general built-ins
    require NM_CONSOLE_BUILTINS
```

## PING

```text
PING
```

Response:

```text
PONG
```

## REBOOT

```text
REBOOT
```

Calls:

```cpp
ESP.restart();
```

This is an immediate device restart operation.

## INFO

Query the complete aggregate:

```text
INFO
```

Query one section:

```text
INFO IDENTITY
INFO HARDWARE
INFO HWCONNECTIONS
INFO BUILD
INFO BOOT
INFO SYSTEM
INFO NETWORK
```

Publication:

```text
INFO PUBLISH
INFO PUBLISH SYSTEM
INFO PUBLISH NETWORK
```

`INFO PUBLISH` with no document argument defaults to the aggregate `/info` document.

Only:

```text
INFO
SYSTEM
NETWORK
```

are MQTT documents. The other section names are query-only.

See [Status, info, and telemetry](status-info.md).

## JOB

All JOB commands operate on Scheduler scope:

```text
USER
```

They cannot list, delete, or clear MANAGED framework/application jobs.

### List

```text
JOB LIST
```

Returns Scheduler JSON containing only USER jobs.

### Delete by label

```text
JOB DELETE night
```

### Delete by ID

```text
JOB DELETE #12
```

The numeric ID form requires the `#` prefix.

### Clear

```text
JOB CLEAR
```

Clears all USER jobs.

It does not clear MANAGED jobs.

### Run once at wall time

```text
JOB AT <label> <epoch_s> "<command>"
```

Example:

```text
JOB AT lights-off 1790000000 "CONFIG SET mode sleep"
```

`epoch_s` is a 32-bit unsigned Unix-seconds value.

### Run once after a monotonic delay

```text
JOB AFTER <label> <delay_ms> "<command>"
```

Example:

```text
JOB AFTER reconnect 5000 "MQTT CONNECT REMOTE"
```

### Repeat

```text
JOB EVERY <label> <WALL|MONO> <interval> "<command>"
```

For:

```text
WALL
```

the interval unit is seconds.

For:

```text
MONO
```

the interval unit is milliseconds.

Examples:

```text
JOB EVERY hourly WALL 3600 "INFO PUBLISH"
JOB EVERY poll MONO 5000 "PING"
```

The interval must be greater than zero.

### USER label namespace

USER job labels are unique within USER scope.

A USER job and MANAGED job may use the same label without blocking each other.

## Scheduler persistence through JOB

Only wall-clock String command jobs persist.

Therefore:

```text
JOB AT ...
JOB EVERY ... WALL ...
```

are persistence-capable.

```text
JOB AFTER ...
JOB EVERY ... MONO ...
```

are runtime-only.

See the Scheduler module documentation for the full job model.

## MQTT

Available when MQTT support is enabled.

### State

```text
MQTT STATE
```

Returns `MQTTStateJson()` after handling:

```json
{
  "state": 2,
  "broker": "remote"
}
```

State values are implementation state codes.

### Connect / switch broker

```text
MQTT CONNECT LOCAL
MQTT CONNECT REMOTE
```

The implementation also accepts:

```text
MQTT CONNECT 1
MQTT CONNECT 2
```

Any other/missing destination requests a connection using the currently selected broker.

### Disconnect

```text
MQTT DISCONNECT
```

### Swap broker

```text
MQTT SWAP
```

Switches between local and remote broker selection.

## CONFIG

`CONFIG` works with persistent settings.

### Read all settings

```text
CONFIG GET
CONFIG GET all
```

### Read one setting

```text
CONFIG GET <name>
```

### Set one setting

```text
CONFIG SET <name> <value>
```

Use quotes or a backtick literal when the value contains spaces.

### Save

```text
CONFIG SAVE
```

For normal `StateStore` changes, persistence may already be save-on-change; this command explicitly calls `PersistentSettings.save()`.

Framework-private keys are implementation details even though the generic CONFIG surface can expose stored keys.

## SYSTEMCONFIGS

`SYSTEMCONFIGS` operates on the in-memory `SystemState`.

Read all:

```text
SYSTEMCONFIGS GET
SYSTEMCONFIGS GET ALL
```

Read one:

```text
SYSTEMCONFIGS GET <name>
```

Set one:

```text
SYSTEMCONFIGS SET <name> <value>
```

Unlike `CONFIG`, this is runtime state rather than persistent settings.

## FS

Filesystem commands are part of the general built-in command set.

### List

```text
FS LIST
```

Returns JSON:

```json
{
  "files": [
    {
      "name": "/configs.json",
      "size": 123
    }
  ]
}
```

Directory traversal is bounded to four recursive levels and the response uses a 4096-byte JSON document capacity.

### Read

```text
FS READ <filename>
```

### Status

```text
FS STATUS
```

Returns fields:

```text
totalBytes
usedBytes
usedPercentage
initialized
```

### Delete

```text
FS DELETE <filename>
```

### Format

The current implementation authorizes formatting when the second argument is:

```text
-p
```

so the accepted form is:

```text
FS FORMAT -p
```

One failure string in the current implementation still says `FS FORMAT CONFIRM`; that message does not match the actual argument check and should be treated as stale text rather than protocol syntax.

## WIFI

Available when WiFi support is enabled.

### IP

```text
WIFI IP
```

### State

```text
WIFI STATE
```

Returns the current Arduino WiFi status code plus a readable state name.

### Scan

Start an asynchronous scan:

```text
WIFI SCAN -s
WIFI SCAN start
```

Read scan state/results:

```text
WIFI SCAN
```

The JSON response uses a `control` field:

```text
scan_started
scan_start_failed
scan_in_progress
scan_done
```

When done, `networks` entries contain:

```text
ssid
rssi
mac
channel
encryptionType
```

### Change credentials

```text
WIFI CHANGE <ssid> <password>
```

Quote arguments containing spaces.

The implementation attempts the new connection before persisting the new credentials and falls back to the previous credentials if the change fails.

### Reconnect

```text
WIFI RECONNECT
```

The command currently returns:

```text
not implemented yet
```

It is present in the command grammar but is not an implemented reconnect operation.

## Optional HTTPSERVER commands

Only present when HTTP is compiled in.

```text
HTTPSERVER PRIORITY <high|normal>
HTTPSERVER STATUS
HTTPSERVER RESET
HTTPSERVER ENABLE <1|0>
```

HTTP is an optional surface that has not received the same architecture pass as the core Resource/network model.

## Optional WS commands

Only present when WebSocket support is compiled in.

```text
WS LIST
WS <message>
```

`WS LIST` reports active clients.

Any other subcommand is broadcast to WebSocket clients by the current implementation.

WebSocket command support is peripheral to the current core architecture.

## TEST

```text
TEST "<command text>"
```

This is a parser/debug command. It runs the older `parseNightMareMessage()` parser over the supplied first argument and reports the parsed fields.

It should not be treated as part of application protocol design.

## Commands vs Resource Actions

Commands and Resource Actions are intentionally different.

Use a **command** for framework/operator control such as:

```text
INFO
JOB
MQTT
WIFI
CONFIG
```

Use a **Resource Action** for an application capability such as:

```text
learn_ir
pair_remote
run_cycle
```

A Resource Action is discoverable through the Resource manifest and belongs to the device's application contract.

A command belongs to the framework/operator command surface.
