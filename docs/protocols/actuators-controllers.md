---
title: Actuators, controllers and Services
description: The three roles on the control side of a device, the state documents that join them, and where a piece of automation should live.
section: protocols
order: 50
---

# Actuators, controllers and Services

Three words, used precisely. Two of them already mean something in the library.

| role | where | what it does |
| --- | --- | --- |
| **Actuator** | device | changes the world on command and reports the state it *believes* it produced. An IR transmitter, a relay, an LED strip. Decides nothing. |
| **Controller** | device | policy. Reads sensors — local or network — and drives actuators. Owns a *state document* and persistent config. Never touches hardware directly. |
| **Service** | client (`src/Services/`) | a proxy for a remote controller, built on `ServerVariable<T>`. It **defines** the command vocabulary and state document the controller must speak. |

The library's `Services/AcController` and a device's own `AcController` are the
same name on opposite sides of the wire: the first *drives* an AC device from
the Dashboard, the second *is* the AC device. Keep them straight by calling the
library classes Services.

## The state document: `<Device>/state`

A controller publishes its state as one JSON document on `state`, on every
change and on a heartbeat. The field names belong to the Service that consumes
it — they are the contract, and a change is made on both sides at once.

One controller per device is the norm. A second one publishes on
`state/<controller>`; no Service routes there yet.

## Services and `ServerVariable<T>`

A Service holds each remote value in a `ServerVariable<T>`, which gives a
client three behaviours without writing them:

- **optimistic** — change the local value and publish; the UI flips at once;
- **asserted** — ~10 s later compare against what the device reported; if it
  never agreed, roll back, so a command that went nowhere is visible;
- **stale** — no report for 10 minutes marks the value unknown, shown as `?`
  rather than as "off".

The Service's `ParseServerState()` feeds the device's `state` document into its
variables; its `Sync()` runs the assert/stale clock from `loop()`.

## Actuator and controller interfaces

```cpp
// actuator: queues, never blocks, reports belief
bool irSend(uint32_t code);
void irActuatorState(JsonObject into);
void irActuatorInfo(JsonObject into);

// controller: config, loop, state, commands
void startAcController();                 // loads config, writes defaults back, registers timers
void acControllerLoop();                  // from Timers: door, then target, then sleep
void acControllerState(JsonObject into);  // the Service's document
NightMareResults acControllerCommand(const NightMareMessage &m);
```

Inputs come into a controller as functions, never as globals — the room
temperature is a `double (*)()`, the door is a network sensor. That is what lets
the same controller take its door from a local reed switch on one device and
from the network on another.

## Where should a piece of automation live?

The backend has its own automation layer — *ControlDevices*: rules that compare
a sensor's last value and set a status, and actions that fire on a status
transition by sending a command template to a device. It overlaps with
device-side controllers, so the split is explicit:

| put it on the device when | put it in the backend when |
| --- | --- |
| it must keep working with the backend, the broker or the WAN down | it spans devices that have no reason to know each other |
| it is time-critical, or protects hardware or people | it is convenience, or an operator wants to edit it without a reflash |
| its inputs are the device's own sensors, or one network sensor | its inputs are several devices' sensors, or history |

An AC controller — target temperature, door pause, morning shutdown — is on the
device by this rule: it is what stops an air conditioner running into an open
door at 3 a.m. whether or not the cloud broker is reachable. The same door→AC
relation *can* be a backend rule; that is a second line, not the first.

## The AC Service contract, v1

What the Dashboard's `AcController` Service sends and expects. An AC device
implements exactly this.

### Commands to `<Device>/console/in`

| command | args | device action |
| --- | --- | --- |
| `SETTEMP <n>` | 18–30 | set the unit's temperature |
| `TARGET <t>` | °C, negative disables | set the room target; `TARGET -1` is shutdown |
| `POWER <0\|1>` | | unit on/off |
| `manualsync <on> <temp>` | `0\|1`, 18–30 | correct the controller's belief about the unit; sends no IR |
| `SLEEP-IN <0\|1>` | | defer tomorrow's morning shutdown |
| `SENDIR <name>` | code name | raw IR code |
| `PAUSEDOORSENSOR <0\|1>` | | suspend door logic |
| `reboot` | | library |

The parser uppercases the command word, so `manualsync` and `MANUALSYNC` are
one command.

### State document

| field | type | meaning |
| --- | --- | --- |
| `AcState` | int | `2` on+target · `1` on · `0` unknown · `-1` off · `-2` off, target set · `-3` off, door open · `-4` off, target, door open |
| `DoorState` | int bitfield | bit0 door open · bit1 door logic paused · bit2 AC paused by door |
| `Temp` | int | unit temperature; **negative = unit off** |
| `Settemp` | double | room target; **negative = control disabled** |
| `CurrTemp` | float | room temperature |
| `Hsleep` | uint32 | the unit's own sleep timer, seconds; 0 none |
| `Ssleep` | uint32 | the controller's sleep timer; 0 none |
| `Door` | uint32 | epoch seconds the door opened; 0 closed |
| `SleepIn` | bool | tonight's sleep-in armed |

The sign conventions on `Temp` and `Settemp` are load-bearing: the Service
derives on/off from them.

## The light and RGB Services

- **Light**: commands on `<Device>/Light` (`1`/`0`, `toggle-force`, `auto`);
  state `{"State": bool, "Automation": <restore timestamp or 0>}`.
- **RGB light**: commands on `<Device>/Color` and `<Device>/Brightness`; state
  adds `"Color"` and `"Brightness"` to the light's document.

The Dashboard's plain-light card is the exception: it reads the light as a
sensor reading (`{"light": true}` in `sensors`) and sends `LIGHT SET toggle`,
joined by a hand-fed `ServerVariable<bool>` — the same three behaviours,
without a `state` document.
