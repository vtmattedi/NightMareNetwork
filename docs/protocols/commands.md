---
title: Commands
description: The command grammar every device speaks, the commands the library answers for you, and how a device adds its own.
section: protocols
order: 30
---

# Commands

One grammar, three transports. A command line typed into the serial monitor,
published to `<Device>/console/in`, or sent over [MQTTP](/docs/protocols/mqttp)
goes through the same parser and the same two-stage resolver.

## Grammar

```
COMMAND SUBCOMMAND ARG1 ARG2 ARG3 ARG4
```

The first token is the command. The rest fill `args[0..4]`, and `subcommand`
is an uppercased copy of `args[0]` — so **a command's own parameters start at
`args[1]`**. Command and subcommand are uppercased; arguments keep their case.

| construct | meaning |
| --- | --- |
| whitespace | any run of space/tab/CR/LF separates tokens; a run counts once |
| `"..."` | quoted section, quotes removed; adjacent sections join (`a"b c"d` → `ab cd`); `""` is present-but-empty |
| `\"` and `` \` `` | the only two escapes |
| `\` anything else | kept as typed — `C:\tmp` survives |
| `` `...` `` | fence: contents verbatim, no escapes; a fence of N backticks closes on exactly N |

Limits: 5 arguments, 512 characters. Malformed input is reported, not guessed
at — `valid` is false and `error` says why (`unterminated quote`, `too many
arguments, limit is 5`, …). The full parser rules, including nesting a command
inside a scheduler task, are in the repository README.

## Two-stage resolution

1. **The library's preprocessor** answers the built-ins below.
2. Anything else goes to the function the device registered with
   `setCommandResolver()`.

A device cannot override a built-in; it can only add commands beside them.

## Built-in commands

| command | subcommands | what |
| --- | --- | --- |
| `PING` | | `PONG`. The backend's adoption check. |
| `REBOOT` | | restart |
| `BOOTINFO` | | reset reason, time-synced, current time, uptime, boot time |
| `HARDWAREINFO` | | chip model/cores/revision, flash, heap, PSRAM, MAC |
| `SYSTEMINFO` | | the same document `telemetry` publishes |
| `TIME` | | current time |
| `FS` | `LIST READ <file> STATUS DELETE <file> FORMAT` | LittleFS |
| `MQTT` | `STATE CONNECT DISCONNECT SWAP` | client state; `SWAP` flips local/remote broker |
| `CONFIG` | `GET <key\|ALL> [-p]`, `SET <key> <value> [-s] [-p]`, `SAVE` | persistent store; `-p` privileged (keys starting with `_`), `-s` save now |
| `SYSTEMCONFIGS` | `GET <key\|ALL>`, `SET <key> <value>` | volatile flags |
| `WIFI` | `IP STATE RECONNECT SCAN CHANGE <ssid> <pass>` | |
| `HTTPSERVER` | `PRIORITY STATUS RESET ENABLE` | when `COMPILE_HTTP_SERVER` |
| `SCHEDULER` | `LIST [-t]`, `CLEAR`, `KILL <id>` | inspect and disarm scheduled tasks. `ADD` and `EDIT` exist as empty branches; scheduling is done with `TASK`. |
| `TASK` | `<label> <command> <interval_s> <execution_time>` | schedule `<command>` at epoch `<execution_time>`, repeating every `<interval_s>`; persisted |
| `TIMERS` | | the interval timers and their time left |
| `WS` | `LIST` | websocket clients, when compiled |

`SYSTEMINFO`, `BOOTINFO` and `HARDWAREINFO` are what the Dashboard's device
page offers, because every device has them. `sensors` is deliberately not on
that page: it is device-local.

## Device commands

The device's resolver handles its own groups. The convention:

```
<GROUP> <VERB> [args]
```

`GROUP` is a component or controller (`IR`, `DS18`, `AC`, `LIGHT`). `VERB` is
one of a small reserved set; a group may add its own but implements the
reserved ones that apply:

| verb | applies to | meaning |
| --- | --- | --- |
| `INFO` | everything | the group's entry in the device descriptor |
| `READ` | sensor | current data, from the last sample — never a fresh bus read |
| `STATE` | actuator, controller | current state |
| `SET <what> <value>` | actuator, controller | change something |
| `TOGGLE [what]` | actuator, controller | flip a boolean |
| `HELP` | everything | the verbs this group accepts |

Root-level device commands: `INFO` (the whole descriptor), `SENSORS REPORT`,
`SENSORS INFO`, **`sensors`** (bare — the declaration the backend reads, see
[Sensors](/docs/protocols/sensors)), `HELP`.

Every "unknown" reply lists what would have been valid:

```
Unknown IR subcommand available: [SEND, LIST, INFO, DEBUG].
```

## Controllers speak their Service's vocabulary

A controller's command set is defined by the client-side Service that drives
it, not by the device. The Dashboard's AC Service sends `SETTEMP 24`; the AC
device accepts `SETTEMP 24`. Aliases in the group grammar (`AC SET TEMP 24`)
are welcome, but the Service's words are the contract, and a change to them is
made on both sides in one step. See
[Actuators and controllers](/docs/protocols/actuators-controllers).

## Scheduling a command

The scheduler stores a command string and feeds it back through the parser at
a wall-clock time, with optional repeat. That makes it the network's cron —
a daily action is a scheduler task that runs a command, not a timer comparing
the time every minute:

```
TASK ac_morning_off `AC MORNINGOFF` 86400 1758182200
SCHEDULER LIST
```

`TASK` takes the label, the command (fenced, because it contains a space),
the repeat interval in seconds and the first execution as an epoch timestamp —
`timestampOfNextOccurrence("05:30")` in firmware gives the latter. Tasks
persist across reboots and re-align when the clock syncs.
