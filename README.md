# NightMare Network Lib

<p align="justify">
This is a library that contains some functions, classes and structures used by different projects across the <i> NightMare Network </i> Which is my own home automation environment. Having this code separeted in here makes it easier to mantain and sync the code used for the same stuff on all the projects. Also makes it easier for me to implement new features as I add them in the controller side, or if I have a better solution to a common problem acoss different devices.
And at Last, with <b>PlatafromIO</b> I can easily set a github repository as a dependancy of the project making the part of creating/porting new projects also easier. This funcion also helps when refactoring old projects to include this (or any other) Lib.
</p>

## Setup

This library is configured by two headers that the **consuming project** supplies, not the library:

| file | what it holds |
| --- | --- |
| `creds.h` | WiFi SSID/password, MQTT host/port/user/password, root CA, GMT offset |
| `Modules.config.h` | the `COMPILE_*` switches that decide which modules get built |

Copy the templates into your project's `include/` folder and fill them in:

```
cp .pio/libdeps/<env>/NightMareNetwork/src/Core/exemple.creds.h        include/creds.h
cp .pio/libdeps/<env>/NightMareNetwork/src/Modules.example.config.h    include/Modules.config.h
```

Then add the project include folder to `build_flags`, so the library's own sources can find them:

```ini
build_flags =
	-I "${platformio.include_dir}"
```

That flag is **required**. PlatformIO puts `include/` on the path when compiling your `src/`, but
libraries are compiled with their own include path and would not see these headers otherwise.

Keeping both files in your project rather than inside `.pio/libdeps/` matters: PlatformIO wipes and
re-clones that folder on `pio pkg update`, and the library gitignores both names, so a copy living
there is not recoverable from either repository. Add `include/creds.h` to your project's
`.gitignore`.

## Structure

The structure of this Lib is as follows:

+ Outter .h
  + Core Folder
    + Commonly used structs and functions throughout differents projects.
  + Services Folder
    + Classes that encapsulates a controller for another device on the network
  + HTTP Folder
    + HTTP and WEBSOCKET servers (mostly just integrating to nightmare services).
  + Xtra Folder
    + Some extra components that are not really core nor a service but is still common code across multiple devices on the network
  + Tcp Folder
    + TCP client & server with integration to other modules & transmission modes.

Headers are layered: `Misc.h` is a leaf that `Timers.h`, `NightMareCommand.h` and
`NightMareNetwork.h` depend on, so it must not reach upward into WiFi/MQTT/HTTP. Anything that
aggregates across modules gets its own file whose *header* declares only the function, with every
heavy include confined to the `.cpp` — `SystemStatus` is the worked example.

## Current Services (Controllers)

1. Ac Controller
2. Light Controller
3. Light with Color Controller

## Core Functionalities

1. Timers.
2. MQTT Client using esp mqtt.
3. Server Variables.
4. Misc. Functions
   + Convert Timestamp to human readable String
   + `ramUsagePercent()` / `fsUsagePercent()`
   + If LVGL is detected also compiles functions to easily hide/show objects and set/clear flags (specialy the Checked)
5. TimeSynchronization function.
6. System Status (`COMPILE_SYSTEMSTATUS`)
   + `getSystemStatus()` returns one JSON blob with uptime, heap, boot time, time-sync state, reset
     reason, WiFi RSSI and IP, MQTT local/remote, HTTP and OTA state, async-command readiness.
   + Modules that are not compiled in report a disabled value rather than vanishing, so the JSON
     keeps the same shape across builds.

## Xtra Functions

1. Scheduler
2. Command Resolver

## Command message grammar

`parseNightMareMessage2()` turns a raw line into `command` / `subcommand` / `args[]`. The first
token is the command, the rest fill `args[0..NM_MAX_ARGS-1]`, and `subcommand` is an uppercased copy
of `args[0]` — so a command's own parameters start at `args[1]`. Command and subcommand are
uppercased; args keep their case.

| construct | meaning |
| --- | --- |
| whitespace | Any run of space/tab/CR/LF/VT/FF separates tokens. A run counts once, so extra spaces never shift an argument's position. |
| `"..."` | Quoted section; the quotes are removed. Adjacent sections join, so `a"b c"d` is the single token `ab cd`. `""` is an argument that is *present but empty*. |
| `\"` and ``\` `` | The only two escapes. They produce a literal `"` and a literal `` ` ``. |
| `\` anything else | Kept exactly as typed, so `/a\b.json` and `C:\tmp` survive unchanged and a backslash never needs doubling. |
| `` `...` `` | Fence. Contents are taken verbatim — no escapes, no quote handling, whitespace as typed. |

A fence opened with N backticks closes on a run of **exactly** N, so content containing a backtick
just opens wider — the common case still costs one character a side:

```
A `WS "hi"`              -> [WS "hi"]
A ``say `hi` ok``        -> [say `hi` ok]
A ```two `` inside```    -> [two `` inside]
```

An empty fence is not expressible (` `` ` reads as an unterminated fence of length 2); use `""`.

### Nesting commands

The scheduler stores a command as an argument and later feeds it back through the parser, so
quoting has to survive a round trip. Fences make that readable, one extra backtick per level:

```
TASK t1 ``TASK t2 `PING` 20`` 30
  -> stored: TASK t2 `PING` 20
  -> stored: PING
```

The escape route works too (`TASK t "WS \"hi\"" 20`), but doubles backslashes at each level.

### Failure reporting

Malformed input is reported rather than guessed at — check `valid` before using the result:

| field | meaning |
| --- | --- |
| `valid` | false when the line could not be parsed |
| `error` | why: `empty command`, `unterminated quote`, ``unterminated ` literal``, `too many arguments, limit is N`, `input too long: N chars, limit is M` |
| `argc` | how many arguments were actually supplied — this is what separates "absent" from "empty string" |

`executeNightMareCommand()` turns an invalid parse into a `Parse error: <error>` response instead of
letting a half-parsed line fall through as "unrecognized".

Limits live in `Xtra/NightMareTypes.h`: `NM_MAX_ARGS` (5) and `NM_MAX_MESSAGE_LEN` (512). The length
cap is enforced in the parser rather than at each caller, because MQTT, HTTP, WebSocket and TCP all
hand it an untrimmed, unbounded payload — only the serial resolver trims. `ensureSize()` is exposed
for checking a payload against a limit before anything else touches it:

```cpp
bool ensureSize(const String &str, size_t maxLength, String &error);
```

> `parseNightMareMessage()` (no `2`) is the original parser, kept for now. It has no error
> reporting, treats only `' '` as a separator, cannot express a literal quote, and drops a quoted
> value in the last argument slot. New code should use `parseNightMareMessage2()`.
