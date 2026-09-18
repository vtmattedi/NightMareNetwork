---
title: Device architecture
description: How a NightMare device is built, what it says on the wire, and how the existing devices get brought in line.
section: architecture
order: 10
---

# NightMare Networks — device architecture

How a NightMare device is built, what it says on the wire, and how the ones that
exist today get brought in line. Written against the code as of September 2026:
the `NightMareNetwork` library at `ed0128e`, Mycroft-headless as the reference
device, the Dashboard and the `MwNightmareSystem` backend as the two consumers
every device has to satisfy, and Adler as the first retrofit.

The base is Mycroft-headless. Where this document departs from it, it says so
and why. Everything else in the network is measured against it.

Turing is out of scope. It runs in its own namespace with its own transport, and
the sensor implementation on disk is older than the one that was lost — what
survives of the newer one is its *contract*, which the backend and the web
frontend still parse (Appendix B). That contract is used here; the Turing repo
itself is not a reference.

---

## 1. Vocabulary

Five words, used precisely from here on. Two of them already mean something in
the library and are kept; the collision between them is the first thing to know.

| term | what it is | lives in |
| --- | --- | --- |
| **Device** | One MQTT identity (`DEVICE_NAME`), one firmware image, one board revision, any number of components. | the project repo |
| **Component** | A unit of hardware the device owns, with its driver. Either a *Sensor* or an *Actuator*. | `include/` + `src/`, one pair per component |
| **Sensor** | A component that produces readings. Exposes *data* (values, with age; `NAN`/`null` when unknown) and *info* (what it is). Never blocks a caller. | device |
| **Actuator** | A component that changes the world on command and reports its *state*. Also has *info*. | device |
| **Controller** | Device-side policy: reads sensors (local or network), drives actuators, owns a *state document* and persistent *config*. Never touches hardware directly. | device |
| **Service** | The library's word, kept: a *client-side proxy* for a remote controller, built on `ServerVariable<T>`. It defines the command vocabulary and state document the controller must speak. | `NightMareNetwork/src/Services/` |
| **Network sensor** | A sensor whose readings arrive from another device's `sensors` topic. Same interface as a local sensor, so a controller does not know or care where a reading comes from. | library (to be added, §6.4) |

**The collision.** The library's `Services/AcController` and Adler's
`lib/components/AcController` are the same class name on opposite sides of the
wire. The library one is a Service (the Dashboard uses it to *drive* an AC
device); Adler's is a Controller (it *is* the AC device). They must not share a
name. This document calls the library classes Services throughout, and the
rename of the classes themselves is open decision D1.

## 2. What the library already gives every device

Do not reimplement any of this. Several devices currently do.

| capability | how it arrives | notes |
| --- | --- | --- |
| Identity on the broker | `<Device>/status` = `online`, retained, on every connect; LWT `offline`, retained | `MQTT.cpp:147,491-499`. The Dashboard treats a device with no retained status as offline. |
| Liveness | `<Device>/telemetry` = `getSystemStatus()` every **15 s** | Created by `TimersHandler`'s constructor (`BOOTSTRAP_TIMER_SYNC`), so it runs whether or not the device asks. Mycroft and the Dashboard create a second telemetry timer on top of it — duplicate publishing, see §8. |
| Console | `<Device>/console/in` and `all/console/in` → command resolver → `<Device>/console/out` | Uppercases command and subcommand; args keep case. |
| Request/response (MQTTP) | `<Device>/console/controlled/<id>/in` → `.../out` | Replies over 512 B are chunked `;;n/total;;<data>`. |
| Built-in commands | `PING REBOOT BOOTINFO HARDWAREINFO SYSTEMINFO TIME FS MQTT CONFIG SYSTEMCONFIGS WIFI HTTPSERVER SCHEDULER TIMERS WS` | Anything not matched falls through to the device's `setCommandResolver()` handler. |
| Timers | `Timers.create(label, interval, cb, use_millis)` | Polled from `loop()`. Callbacks run on the loop task. |
| Scheduler | `scheduler` — persisted to `/scheduleTasks.json`, fires a **command string** through the resolver at a wall-clock time, with optional repeat | This is the network's cron. A daily action is a scheduler task that runs a command, not a timer that compares `TIME_STR` every minute. |
| Config | `Config` (persistent, `/configs.json`) and `SystemSettings` (volatile flags) | `Config.begin()` must run before anything reads it. |
| Time | `Control/request` = `time` answered by the backend on `Control/time`; consumed inside the MQTT client | Devices never parse it themselves. |
| ServerVariable | Optimistic local value, publish, assert against the server after 10 s, roll back if not confirmed, stale after 10 min | The mechanism every Service is built on. |
| Device name | Runtime, `Config` key `_device_name`, default `Esp32-nm-<efuse>` | Anything that addresses another device does so by this name, at runtime, from config — never a compile-time constant. |
| Adoption | Backend sees a fresh `Esp32-nm-*` on an identity channel → `ping` over MQTTP must answer `PONG` → operator names it → `CONFIG SET _device_name "<name>" -p` must reply `{"...","saved":true}` | Both halves are in the library today (`NightMareComand.cpp:367,637`). The backend then clears the factory name's retained `status`/`state`/`ai_state`. |
| Discovery read | On first sight per process, and again each time a device comes back online, the backend runs **`bootinfo`, `sensors`, `config get ALL -p`, `HARDWAREINFO`** sequentially over MQTTP and persists the four replies | `bootinfo`/`config`/`HARDWAREINFO` are library built-ins. **`sensors` is not** — see §7. |
| `telemetry` shape | The backend does `JSON.parse(payload).System` and stores that as the device's info | The top-level `System` key in `getSystemStatus()` is load-bearing; a flat telemetry document would be ignored. |
| MQTTP limits | Backend: 20 s timeout, 256 KB cap, replies chunked at 512 B. Dashboard: 8 s, 3 KB. | The backend also speaks an `asynccontrolled` variant (`;;:;;finished;;` terminator, `;;error;;` chunks). No device implements it; the library only matches `/console/controlled/`. |

**The threading rule the library imposes and does not enforce.** The MQTT
message callback runs on the esp-mqtt task, not on `loop()`. On a dual-core
chip that is true parallelism with `loop()`; on the C3 it is preemption. Either
way, application state touched from both sides corrupts. The Dashboard learned
this the hard way ("exhausted after two minutes") and its answer is the rule for
every device: **the MQTT callback only enqueues; `loop()` does the work.** See
§5.3.

## 3. The wire contract

Every topic below is under `<Device>/` unless marked raw. `MQTT_Send()`
prepends the device name; `MQTT_Send_Raw()` does not.

### 3.1 Topics a device publishes

| topic | payload | when | retained |
| --- | --- | --- | --- |
| `status` | `online` / `offline` | library | yes |
| `telemetry` | system status JSON | library, 15 s | no |
| `sensors` | **one JSON object**, one field per sensor key, scalar values | on any change, and on a heartbeat interval (60 s) | no — D2 |
| `info` | device descriptor: board, sensors, actuators, controllers (§3.4) | on MQTT connect, and on `INFO` | no |
| `state` | the controller's state document (§3.5) | on any change, and on the heartbeat | no |
| `console/out`, `console/controlled/<id>/out` | command replies | library | no |

Readings go on **`sensors` as one object**, never as bare scalars on
`sensors/<key>`. The Dashboard accepts both, expanding an object into one
registry entry per field (nested objects join keys with `/`), but the object
form is what Mycroft-headless publishes, it is one message instead of N, and it
carries the *absence* of a sensor as `null`. Adler's current
`sensors/temperature` scalar is the legacy form.

`info` is a new channel rather than Mycroft-headless's `sensorsinfo` because
`sensors/<anything>` is ingested by both the Dashboard and the backend as a
reading named `<anything>` — `sensors/info` would register a sensor called
"info". `info` is also where the actuators and the board go, which `sensorsinfo`
never held. Mycroft-headless keeps publishing `sensorsinfo` until it is
retrofitted; nothing consumes it today.

**Readings, as the backend stores them.** These are ingest rules in
`SensorManager.ts`, and they decide how a value looks in Postgres forever:

- `""`, `null`, `nan` and `undefined` are *not stored* — they mean "no reading".
  So a sensor with nothing to say publishes `null`, and its history has a gap
  rather than a zero.
- `true`/`false` are stored as `1`/`0`. Publish booleans as booleans.
- **Type sticks to the first reading.** A temperature that first arrives as
  `23` is an `int` for the life of the row. Publish floats with a decimal point
  always (`23.0`), which `ArduinoJson` does for a `float`/`double` field.
- Unit and label come from the device's declaration (below); with no
  declaration the backend guesses `°C` from a key containing `temp` and `%`
  from `humid`, and names the sensor `<device>/sensors/<key>`.

**The declaration.** The backend asks every device `sensors` — the bare word,
no subcommand — on discovery and again whenever it comes back online, and
parses the reply as an object keyed by sensor id:

```json
{
  "temperature": { "id": "temperature", "label": "Room temperature", "unit": "°C",
                   "type": "float", "disable": false, "critical": false,
                   "hardware": "DS18B20", "pin": 10, "connected": true, "address": "28FF640E2F1A3C02" },
  "door":        { "id": "door", "label": "Door", "unit": "", "type": "boolean",
                   "disable": false, "critical": true,
                   "hardware": "reed switch on PCF8574", "pin": 4, "connected": true }
}
```

The backend reads `id`, `label`, `unit`, `disable` and `critical`; **the id must
equal the key the reading is published under.** Everything else is for people
and for the Dashboard — `type` uses the backend's own enum
(`int|float|string|boolean`), and `hardware`/`pin`/`connected`/`address` are
Mycroft-headless's `SensorsInfo()` fields. That is the whole `sensors` block of
`info` (§3.4), and `SENSORS INFO` returns the same object, so one function
serves all three.

### 3.2 Topics a device consumes

| topic | who publishes | what the device does |
| --- | --- | --- |
| `<Self>/console/in`, `all/console/in` | anyone | library runs the resolver |
| `<Self>/console/controlled/<id>/in` | Dashboard, backend | library runs the resolver, replies on `/out`. The backend's `asynccontrolled` variant is not implemented device-side. |
| `<Other>/sensors` | another device | a **network sensor** (§6.4) picks its key out; nothing else reads other devices' readings |
| `<Other>/status` | another device | network sensors use it for liveness |
| `Control/time` (raw) | backend | library |

A device subscribes to `#` (the library does). It must **ignore** everything not
in this table, and it must do the ignoring on the MQTT task, cheaply, by topic
shape — see the Dashboard's `classify()`.

### 3.3 Command grammar

```
<GROUP> <VERB> [arg ...]
```

`GROUP` is a component or controller name. `VERB` is one of a small reserved
set; groups may add their own but must implement the reserved ones that apply:

| verb | applies to | meaning |
| --- | --- | --- |
| `INFO` | everything | the group's entry from the `info` descriptor |
| `READ` | sensor | current data (never triggers a blocking read) |
| `STATE` | actuator, controller | current state |
| `SET <what> <value>` | actuator, controller | change something |
| `TOGGLE [what]` | actuator, controller | flip a boolean |
| `HELP` | everything | verbs this group accepts |

Device root commands, outside any group: `INFO` (whole descriptor), `SENSORS
REPORT` (the `sensors` object), `SENSORS INFO`, `HELP` (groups). Every "unknown
subcommand" reply lists the valid ones, the way Mycroft-headless already does.

A controller's command set is **defined by its Service**, not by the device.
If the Dashboard's `AcController` Service sends `SETTEMP 24`, the AC device
accepts `SETTEMP 24`. Aliases in the new grammar (`AC SET TEMP 24`) are
welcome, but the Service's vocabulary is the contract and changes to it are
made on both sides in one step. Appendix A is that vocabulary for the AC, as
deployed today.

### 3.4 The `info` descriptor

```json
{
  "device": "Adler",
  "firmware": "1.1.130",
  "board": { "name": "ESP32-C3 SuperMini rev1", "revision": "BOARD_C3_V1",
             "connections": [ { "gpio": 7, "device": "IR RX demodulator", "level": 1, "notes": "..." } ] },
  "sensors": {
    "temperature": { "id": "temperature", "label": "Room temperature", "unit": "°C", "type": "float",
                     "disable": false, "critical": false, "hardware": "DS18B20", "pin": 10,
                     "connected": true, "address": "28FF640E2F1A3C02", "value": 23.44, "age_ms": 1200 }
  },
  "actuators": {
    "ir": { "hardware": "IR LED + TSOP", "pins": { "tx": 8, "rx": 7 }, "codes": ["POWER", "PLUS", "..."] }
  },
  "controllers": {
    "ac": { "service": "AcController", "config": { "ac_hysteresis": 0.5, "ac_turn_off_time": "05:30" } }
  }
}
```

The sensor entry is the declaration from §3.1 with `value`/`age_ms` added, so a
single message answers "what is it and is it working". `board` is Adler's
`boardinfo.h` table. The library's own `HARDWAREINFO` stays chip-level; `info`
is the device-level complement.

### 3.5 State documents

One controller per device is the norm and `state` is its document. The field
names are the Service's business (Appendix A for the AC; `{"State", "Automation"}`
for the light Service; `{..., "Color", "Brightness"}` for the RGB one). A device
with a second controller publishes it on `state/<controller>`, and no Service
routes there yet.

### 3.6 Control that lives in the backend

The backend has its own automation layer, and it overlaps with device-side
controllers, so the split has to be explicit.

**ControlDevices** (`ControlDeviceService.ts`): a named status machine. Rules
compare a sensor's last value (`> >= < <= == !=`) and set a status; the
latest-matched rule wins; a status *transition* fires actions, each an MQTTP
command template sent to a target device, with `{{sensor.<device>.<key>}}`,
`{{status}}`, `{{previousStatus}}` substituted. Evaluated on every sensor
update. **QuickActions**: a named command with `{0}`-style inputs, run on
demand from the web UI.

The rule for where a piece of policy lives:

| put it on the device when | put it in the backend when |
| --- | --- |
| it must keep working with the backend, the broker or the WAN down | it spans devices that have no reason to know each other |
| it is time-critical or protects hardware or people | it is convenience, or an operator wants to edit it without a reflash |
| its inputs are the device's own sensors, or one network sensor | its inputs are several devices' sensors, or history |

Adler's AC controller — target temperature, door pause, morning shutdown — is
on the device by this rule: it is the thing that keeps an air conditioner from
running into an open door at 3 a.m. whether or not HiveMQ is reachable. The
same door→AC relationship *can* be expressed as a backend rule
(`{{sensor.<door-device>.door}} == 1` → `POWER 0` on Adler); that is a
legitimate second line, not the first one.

A device does nothing special to take part: it publishes readings that the
rules read, and answers console commands that the actions send. Every command
in §3.3 is therefore also an action target.

## 4. Firmware layout

Mycroft-headless's layout, with the board registry from Adler on top.

```
platformio.ini            envs: one per board revision, each with -D BOARD_<CHIP>_V<N>
include/
  creds.h                 gitignored; from the library's exemple.creds.h
  Modules.config.h        tracked; COMPILE_* switches, no board-specific defines
  board.h                 append-only revision registry (§5.5)
  boardinfo.h             connections table, GPIOs taken from board.h
  <Component>.h           one per sensor / actuator
  <Controller>.h
  Version.h               generated
src/
  main.cpp                setup(), loop(), the command resolver, the reports
  <Component>.cpp
  <Controller>.cpp
  Version/script.py
lib/                      vendored third-party only; never project code
```

Project code goes in `include/` + `src/`, not in `lib/<name>/`. A `lib/`
library is compiled with its own include path, does not see `include/` without
the `-I` flag, and the LDF has to be persuaded to build it — Adler's
`lib/components` is the last holdout. The library's own README now requires the
`-I "${platformio.include_dir}"` flag anyway, so `lib/` buys nothing.

## 5. Rules

### 5.1 Setup order

```cpp
void setup()
{
    Config.begin();                        // 1. first: everything below may read it
    Serial.begin(115200);
    Serial.println(getDeviceName()); ...   // 2. banner: name, VERSION, BUILD_TIMESTAMP
    printBoardInfo();                      // 3. board + live pin levels
    setCommandResolver(localHandleNightMareCommand);
    WiFi_onConnected(onWifiConnected);     // 4. onWifiConnected -> MQTT_Init(REMOTE_MQTT) on first connection
    WiFi_Auto();
    MQTT_onMessage(onMqttMessage, false);  // 5. enqueue only -- see 5.3
    MQTT_onConnected(onMqttConnected);     //    flag only; publishes happen from loop()
    setupSensorX(); setupActuatorY();      // 6. components: never block, start their own tasks if needed
    startControllerZ();                    // 7. controllers: load config, register timers/scheduler
    Timers.create("sensors_heartbeat", 60, []{ publishSensors(); });
}

void loop()
{
    Timers.run();
    scheduler.run();
    Net_loop();                            // drain the MQTT inbox, then <Controller>.sync()
    NightMareCommand_SerialResolver(&Serial, '\n');
}
```

The Dashboard defers `MQTT_Init` to `loop()` behind a heap check because TLS
needs ~60 KB contiguous and a failed `esp_mqtt_client_init()` boot-loops. On a
device with a free heap that is not a constraint; on anything with LVGL or a
display buffer it is, and the deferral pattern is the answer.

### 5.2 Components never block

A component either:

- **runs a non-blocking state machine on a `Timers` callback** — Adler's
  `Sensors.cpp` (request conversion, come back next tick), or
- **owns a FreeRTOS task** — Mycroft-headless's `TempSensor` (sleep through the
  750 ms conversion) and `LightController` (the ADC busy-wait is a full mains
  period and cannot be broken up).

Either way every public getter returns a **snapshot** copied under a spinlock
(`portENTER_CRITICAL`), so a caller never sees half an update, and `NAN` means
"no reading" — `ArduinoJson` serialises it as `null`, which is what the
consumers expect. A component that finds its hardware missing keeps looking
(`TEMP_MAX_FAILURES`, retry every interval) rather than giving up at boot.

Tasks sit out OTA: check `SystemSettings.getFlag("ota_running")` before
touching a bus whose bit-banging masks interrupts.

### 5.3 The MQTT callback only enqueues

On the MQTT task: classify by topic shape without allocating, copy into a
fixed-size ring buffer, return. Drop and count when full — a blocked MQTT task
misses keep-alives and the broker drops the connection. On `loop()`: drain a
bounded number per pass and route. Every piece of application state is then
touched by one task. The Dashboard's `Net.cpp` is the reference implementation
and is worth lifting into the library as-is (D5).

Mycroft-headless gets away with calling `IoXpander.togglePin()` from the
callback because that class has its own recursive mutex. That is the exception
that needs the comment, not the rule.

### 5.4 Config

- Persistent keys are namespaced by controller: `ac_hysteresis`, `ac_turn_off_time`.
- A controller's `init()` reads every key with its default and **writes the
  default back**, so a fresh unit's config file is complete and editable over
  `CONFIG SET`. Adler's `AcController::init()` already does this.
- Anything that addresses another device — the door sensor, a target — is
  runtime config by device name and key, never a literal: `door_device`,
  `door_key`, `door_invert`. These are the Dashboard's key names, on purpose,
  so the same sensor is described the same way everywhere.

### 5.5 Board revisions

Adler's `include/board.h` is the pattern for every device:

1. Append only. A shipped revision's block is the only record of how that unit is wired.
2. Exactly one selected, with `-D BOARD_<CHIP>_V<N>` from the env; the header errors on none or more than one.
3. Pins are macros, so unselected revisions cost zero flash. Runtime tables (`boardinfo.h`) derive from the selected revision's macros only.
4. Record pins the firmware does not use.
5. Names are permanent.

Mycroft-headless's `#ifdef ESP32_C3` in `HwInfo.h` and `TempSensor.h` is the
same idea with one axis (chip) and no versioning; it migrates to the registry
when that device is retrofitted. `boardinfo.h` gives `printBoardInfo()` at boot
and the `board` block of `info`, and `isUsableGpio()` guards the pin-poking
debug commands per revision.

### 5.6 Diagnostics every device carries

Adler's debug set, generalised. They cost a few hundred bytes and have already
paid for themselves once.

| command | what |
| --- | --- |
| `BOARDINFO` | the connections table with live pin levels |
| `SETPIN <gpio> <H\|L>` | drive a pin; warns when a component owns it; refuses flash/USB pins |
| `<sensor> READ` / `INFO` | the snapshot, never a fresh bus transaction |
| `DS18 PROBE <gpio>` | throwaway bus on any pin, for finding a sensor (renamed from `DS18 READ <pin>` so it cannot be confused with the non-blocking `READ`) |
| `<actuator> DEBUG ON\|OFF` | verbose logging, persisted as `<actuator>_debug` |

## 6. Component interfaces

Concrete shapes. Plain functions and a status struct, as in Mycroft-headless,
not a class hierarchy: the components are singletons, and a struct copied under
a lock is the whole thread-safety story.

### 6.1 Sensor

```cpp
struct TempSensorStatus { bool connected; bool parasite; float tempC; uint32_t lastReadMs; char address[17]; };

void             setupTempSensor();      // starts sampling; returns immediately
TempSensorStatus tempSensorStatus();     // snapshot under lock
float            currentTemperature();   // NAN when unknown
void             tempSensorInfo(JsonObject into);   // its entry in the info descriptor
void             tempSensorReport(JsonObject into); // its field(s) in the sensors object
```

The two JSON writers are what `SensorsReport()` / `SensorsInfo()` call, so
adding a sensor to a device is: one `.h/.cpp`, one line in each aggregator, one
command group.

### 6.2 Actuator

```cpp
void   setupIrActuator();
bool   irSend(uint32_t code);            // queues; never blocks; false when the queue is full
bool   irSendByName(const String &name);
void   irActuatorState(JsonObject into); // what the AC unit is believed to be doing
void   irActuatorInfo(JsonObject into);
```

An actuator tracks the state it *believes* it has produced (the IR controller's
`IrState`) and exposes it, but it does not decide anything. Adler's
`IrController` is already this, minus the two JSON writers.

### 6.3 Controller

```cpp
void startAcController();                // loads config, writes defaults back, registers timers
void acControllerLoop();                 // called from Timers every N s: door, then target, then sleep
void acControllerState(JsonObject into); // the Service's state document -- Appendix A
void acControllerInfo(JsonObject into);  // service name + effective config
NightMareResults acControllerCommand(const NightMareMessage &m);  // the Service's vocabulary
```

Inputs come in as functions, never as globals: `getCurrentTemp` is already a
pointer in Adler's `AcController`; the door becomes one too. That is what lets
the same controller take its door from a local reed switch on one device and
from the network on another.

### 6.4 Network sensor (new, library)

```cpp
struct NetworkSensorStatus { bool bound; bool fresh; bool hasValue; String raw; uint32_t lastSeenMs; };

class NetworkSensor {
public:
    void  bind(const String &device, const String &key, bool invert = false);  // from Config
    bool  offer(const String &topic, const String &payload);   // from the loop-side router
    NetworkSensorStatus status();
    bool  asBool(bool &out);   // "1"/"true"/"0"/"false" only; anything else is not a reading
    float asFloat();           // NAN when none
};
```

- Bound by *runtime* device name and key, persisted as `<name>_device`,
  `<name>_key`, `<name>_invert` — the Dashboard's `SensorBindings` keys.
- `offer()` is called from the loop-side router with every `<Other>/sensors`
  message; it expands a JSON object the way the Dashboard's `ingestSensor()`
  does and keeps only its own key.
- `fresh` goes false after `MILLIS_TO_STALE` (10 min, matching `ServerVariable`)
  or on a retained `offline` from that device. A controller treats a stale door
  as **unknown**, not as closed.
- Boolean parsing accepts only the two spellings the network uses; a reed
  switch's polarity is part of the binding.

This is the Dashboard's `SensorBindings` + `Registry_sensorValue` with the UI
removed, and it is what Adler's door needs.

## 7. Where the devices are today

| device | library | layout | sensors publish | state publish | commands | board pins | threading |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **Mycroft-headless** | yes | `include/`+`src/`, one file per component | `sensors` JSON object + `sensorsinfo` | — (light is a sensor reading) | `IOXP DS18 LIGHT SENSORS HELP`, listed on error | `HwInfo.h`, `#ifdef ESP32_C3` | components on tasks; MQTT callback pokes IoXpander (mutexed) |
| **Dashboard** | yes | `src/App/` + `src/UI/` | none (no hardware) | none | `TARGET FORECAST HEAP` | n/a | ring buffer, loop-only routing — **the reference** |
| **Adler** | yes | `lib/components/` | `sensors/temperature` bare scalar | **none** | `SENSORS AC IR DS18 SETPIN BOARDINFO` | `board.h` registry — **the reference** | Timers-based, no MQTT input at all |
| **Mycroft** | yes | `lib/MyComponents/` headers with bodies | — | `state` `{State, Automation}` via own `MqttSend` | stub | `pins.h` | — |
| **Sherlock** | no (own PubSubClient) | `lib/Components/` | — | `state` `{Automation, State, Color, Brightness}` | own | literals in headers | — |
| *Turing* | *separate namespace; not retrofitted here. On-disk sensor code predates the lost rewrite; the rewrite's contract survives in the backend and frontend — Appendix B.* | | | | | | |

Four findings from the survey that shape the retrofit:

**Adler no longer speaks the AC contract.** The Dashboard's `AcController`
Service — and, per its README, the backend — drive an AC device by publishing
`SETTEMP`, `TARGET`, `POWER`, `manualsync`, `SLEEP-IN` to its console and
reading `{AcState, DoorState, Temp, Hsleep, Ssleep, Settemp, SleepIn, Door,
CurrTemp}` from its `state`. That is what the *previous* Adler firmware
(`git show HEAD:src/older.main.cpp.text`) implemented. The current Adler
publishes no `state` and accepts `AC POWER|TEMP|STATE` instead. Binding the
Dashboard's AC card to Adler today yields a greyed card and commands that go
nowhere.

**The AC contract itself has two loose ends.** The Service defines a `DOOR`
message that is never emitted (no `on_send_with_info` is assigned to
`DoorState`/`doorOpen`), while the old device accepted `DOOROPEN`. And the
Service sends `PAUSEDOORSENSOR`, which the old device does not handle. Neither
has ever worked; both get settled in Appendix A.

**Adler's door has no source.** `AcController::setDoorOpen()` exists and the
pause/stop logic around it is sound, but nothing calls it — there is no local
sensor and no network input. Mycroft-headless publishes a `door` field in its
`sensors` object from a reed switch on its PCF8574. That is the sensor; §6.4 is
the plumbing.

**No library device answers `sensors`.** The backend asks it of every device on
discovery (§2). Mycroft-headless replies "Unknown SENSORS subcommand"; Adler
replies with its `SENSORS INFO` shape only when asked with the subcommand;
neither answers the bare word. The backend stores whatever came back as the
device's declaration, `parseDeclaredSensors()` finds no object in it, and every
sensor on the network is running on inferred metadata — labels like
`Adler/sensors/temperature`, units guessed from the key. One handler for the
bare `sensors` command (§3.1) fixes it for the backend and gives the Dashboard
`info` at the same time.

## 8. Adler: the retrofit

Adler is a device with three components and one controller:

| | component | today | target |
| --- | --- | --- | --- |
| sensor | DS18B20 | `lib/components/Sensors.*`, Timers state machine | `include/TempSensor.h` + `src/TempSensor.cpp` — Mycroft-headless's file, with `DS18B20_PIN` replaced by `PIN_ONE_WIRE` from `board.h`. Its task-based read and failure handling are better than Adler's; take it whole. |
| actuator | IR transmit + receive | `lib/components/IrController.*` | `include/IrActuator.h` + `src/IrActuator.cpp`. Same code, same table, plus `irActuatorState()`/`irActuatorInfo()`. Command group `IR` unchanged. |
| sensor (network) | door | nothing | `NetworkSensor door;` bound from `door_device`/`door_key`/`door_invert`. |
| controller | AC | `lib/components/AcController.*` | `include/AcController.h` + `src/AcController.cpp`, speaking Appendix A. |
| — | status LED | `PIN_ONBOARD_LED` defined, unused | out of scope; stays defined. |

**State document.** `acControllerState()` builds Appendix A's JSON; published
on every controller state change and on the 60 s heartbeat. `AcState` comes
from the library's `AcStates.h` enum. `CurrTemp` is `currentTemperature()`.
`Door` is the open timestamp the controller already keeps.

**Commands.** `acControllerCommand()` handles the Appendix A vocabulary at the
root (they arrive as bare `SETTEMP 24`, not `AC SETTEMP 24`, because that is
what the Service sends) and `AC ...` aliases in the new grammar. The existing
`AC STATE|POWER|TEMP` handlers move under it.

**Door.** `startAcController()` binds `door` from config and sets the
controller's door input to a lambda over `door.asBool()`. `Net_loop()` offers
every `<Other>/sensors` message to it. A stale or unparseable door is unknown:
the door-pause logic does not run, and `DoorState` reports it. The binding is
made once, from the console: `CONFIG SET door_device "<Mycroft-headless's name>"`,
`CONFIG SET door_key door`, `CONFIG SET door_invert 0|1` after checking which
way the reed reads.

**Morning shutdown.** Replace `autoTurnOff()`'s minute-by-minute string compare
with a command the scheduler fires:

- `AC MORNINGOFF` — if the AC is on, turn it off and disable the target; if
  `sleepIn` is set, do nothing and clear `sleepIn` (it was consumed).
- Two persisted scheduler tasks, created by `startAcController()` if absent:
  `ac_morning_off` daily at `ac_turn_off_time`, and `ac_sleep_in_off` daily at
  `ac_sleep_in_turn_off_time`. The first respects `sleepIn`; the second is the
  fallback the sleep-in defers to. Both run `AC MORNINGOFF`; the command holds
  the policy, the scheduler holds the clock, and `SCHEDULER LIST` shows the
  user what will happen and when.

**Threading.** Add the ring-buffer router from the Dashboard (or the library
piece from D5). Adler currently has no MQTT input, so this is new code, not a
migration.

**Two bugs to carry over as fixed.** `setTemp()` busy-waits on `state.temp`
while holding `loop()`, so the 10-slot IR queue cannot drain during it and a
change of more than 10 steps (the range is 12) spins until the watchdog fires —
it must yield or the queue must be sized to the range. And self-reception: if
the TSOP hears the unit's own IR LED, the receive path applies `nextState()`
again; the tx path should mark a window during which its own code on rx is
dropped.

**Order of work.** Layout move first (mechanical, verifiable by a build), then
`state` + Appendix A commands (restores the Dashboard), then the door, then the
scheduler tasks, then the router. Each step builds and flashes on its own.

## 9. The other devices

In the order that pays back soonest.

1. **Mycroft-headless** — already the base. Three changes: `sensorsinfo` →
   `info`; `HwInfo.h` → `board.h` registry with `BOARD_C3_V1` and
   `BOARD_ESP32_V1`; the light's `automation_restore` pattern (from Mycroft and
   Sherlock) formalised as a controller with `state` if the Dashboard's light
   card ever needs "Restore auto" back. Delete the `IOXP REINIT` default-pin
   trick (`toInt() | 22`) — it ORs the pin, it does not default it.
2. **Dashboard** — nothing structural. Remove its own `Telemetry` timer (the
   library publishes every 15 s already; the panel's 60 s one is a duplicate).
   Once D5 lands, replace `Net.cpp`'s inbox with the library's.
3. **Mycroft** — same hardware family as Mycroft-headless with a display.
   Retrofit is: take Mycroft-headless's components, keep the UI. Its
   `LightHandler` is the automation-with-restore logic that becomes the light
   controller.
4. **Sherlock** — pre-library. Its `LightColorController` (FastLED animations)
   becomes an actuator; its `handleLedAutomation` a controller with the
   `{Automation, State, Color, Brightness}` state document the RGB Service
   already expects. The `state` field names stay.
Turing is not on this list. It is a separate namespace with its own transport
and web UI, and the code on disk is not the version to learn from. What the
lost rewrite got right is preserved as a contract on the consumer side — the
`sensors` declaration in §3.1 *is* that contract, and it is where the id / label
/ unit / disable / critical fields come from.

## 10. Open decisions

Things this document takes a position on but that change other repos, so they
are decided explicitly rather than by default.

- **D1 — Rename the library's `Services/*Controller` classes to `*Service`.**
  Recommended. The collision with device-side controllers is real
  (`AcController` exists on both sides today). Touches the Dashboard's
  `Net.cpp` and `ScreenFocus.cpp`.
- **D2 — Retain `sensors`?** Recommended no. A retained object means a consumer
  that boots sees last-known values with no age; the Dashboard already treats
  an empty retained payload as a tombstone and would need a rule for a stale
  one. Freshness is the network sensor's job (§6.4).
- **D3 — `info` vs `sensorsinfo`.** Recommended `info`, for the reason in §3.1.
  Nothing consumes `sensorsinfo` today, so there is no compatibility cost.
- **D4 — AC vocabulary: keep the legacy words as the contract, or move the
  Service to `AC SET TEMP`?** Recommended keep, and add the aliases. The legacy
  words are deployed in the Dashboard and documented for the backend; the
  aliases cost nothing. Revisit when the backend is in hand.
- **D5 — Lift the Dashboard's inbox/router and `SensorBindings` into the
  library** as `Core/Inbox` and `Core/NetworkSensor`. Recommended; it is the
  only way the threading rule gets enforced rather than described.
- **D6 — Telemetry interval.** The library's 15 s is chatty for a network of
  small devices on a cloud broker. 60 s matches what every device that added
  its own timer chose. Change the library, delete the per-device timers.
- **D7 — Put the bare `sensors` handler in the library.** Recommended. Every
  device needs it (§7), the shape is fixed by the backend, and a device-side
  registry of `sensorInfo(JsonObject)` writers is the natural place to answer
  both `sensors` and `SENSORS REPORT` from. It is the one piece of §6.1 that is
  the same on every device.

---

## Appendix A — AC Service contract, v1 (as deployed)

What the Dashboard's `AcController` Service sends and expects. This is the
contract Adler's controller implements. Source: `Services/AcController.cpp`,
`AcStates.h`, and the previous Adler firmware.

### Commands to `<Device>/console/in`

| sent by the Service | args | device action |
| --- | --- | --- |
| `SETTEMP <n>` | 18–30 | set the unit's temperature |
| `TARGET <t>` | °C; negative disables | set the room target; `TARGET -1` is Shutdown |
| `POWER <0\|1>` | | unit on/off |
| `manualsync <on> <temp>` | `0\|1`, 18–30 | correct the controller's belief about the unit; sends no IR |
| `SLEEP-IN <0\|1>` | | defer tomorrow's morning shutdown |
| `SENDIR <name>` | code name | raw IR code |
| `PAUSEDOORSENSOR <0\|1>` | | suspend door logic — **never handled by any device; implement** |
| `DOOR <v>` | | **defined in the Service, never emitted** (no `on_send` on `DoorState`); the old device's `DOOROPEN` is likewise dead. Drop both; the door is a network sensor. |
| `reboot` | | library |

The parser uppercases the command word, so `manualsync` and `MANUALSYNC` are the
same command.

### State document on `<Device>/state`

| field | type | meaning |
| --- | --- | --- |
| `AcState` | int, `AcStates.h` | `AC_ON_TARGET 2`, `AC_ON 1`, `AC_UNKNOWN 0`, `AC_OFF -1`, `AC_OFF_TARGET -2`, `AC_OFF_DOOR_OPEN -3`, `AC_OFF_TARGET_DOOR_OPEN -4` |
| `DoorState` | int bitfield | bit0 door open · bit1 door logic paused · bit2 AC paused by door |
| `Temp` | int | unit temperature; **negative = unit off** |
| `Settemp` | double | room target; **negative = control disabled** |
| `CurrTemp` | float | room temperature (the DS18B20) |
| `Hsleep` | uint32 | unit's own sleep timer, seconds; 0 none |
| `Ssleep` | uint32 | controller's sleep timer; 0 none |
| `Door` | uint32 | epoch seconds the door opened; 0 closed |
| `SleepIn` | bool | tonight's sleep-in armed |

The sign conventions on `Temp` and `Settemp` are load-bearing: the Service
derives on/off from them (`AcTemperature.value > 0`, `CurrentTarget() > 0`).

## Appendix B — What the backend does with a device

`MwNightmareSystem/backend`, read at the same time as the firmware. This is the
second consumer, and the one that keeps history.

### Identity and discovery (`DeviceManager.ts`)

- Subscribes to `#`. Only `status`, `state` and `ai_state` can *create* a
  device; an empty payload on them is a tombstone and never creates one.
  Readings from an unknown device are dropped — they are usually retained by a
  device that was deleted.
- Topic roots `all` and `n8n` are ignored (operator-editable list). `Control/`
  and `n8n/` are the backend's own channels.
- A device named `Esp32-nm-*` is unadopted: the backend pings it, and only if
  it answers `PONG` does it appear in the adoption list. Adoption renames it
  with `CONFIG SET _device_name "<name>" -p` and requires `saved:true` in the
  reply, then clears the old name's retained identity channels.
- Deleting a device clears its retained `status`, `state`, `ai_state` and every
  `sensors` topic it was seen on — the same set the Dashboard clears.
- On discovery and on each return to online: `bootinfo`, `sensors`,
  `config get ALL -p`, `HARDWAREINFO`, run one at a time, persisted as four
  text columns. `telemetry`/`ai_state` → `.System` → `deviceInfo`.

### Readings (`SensorManager.ts`)

- `<device>/sensors[/<key>]`; a JSON object expands one level per field, nested
  objects join keys with `/`; a scalar needs a key in the topic.
- Dropped: `""`, `null`, `undefined`, `nan`. Normalised: `true`→`1`,
  `false`→`0`. Type inferred from the first reading and kept.
- Every reading goes to a TimescaleDB hypertable, flushed every 60 s. Every
  non-console topic's last payload is also kept in `MqttData`, which is what
  the web UI's topic tree shows.
- Metadata ownership: `auto` rows are re-derived from the declaration on every
  ingest; a row an admin edited is `user` and never touched again.

### Control (`ControlDeviceService.ts`, `quickActionController.ts`)

See §3.6. Actions and quick actions reach a device over MQTTP with the
backend's 20 s timeout, so a command that takes longer to answer (a blocking
sensor probe, a `setTemp` walking twelve steps at 200 ms each) reads as a
failure even when it worked.

### Dead ends, kept so nobody rediscovers them

| what | where | status |
| --- | --- | --- |
| `All/control` = `get_info` published on every backend connect | `MqttService.ts:46` | Nothing handles it. The library's broadcast channel is `all/console/in`, lower-case, different channel. |
| `asynccontrolled` MQTTP | `HttpOverMqtt.ts` | No device implements it. |
| `sensorsdata`, `muxdata [-q]`, `sipo [-q]`, `systemstate` polling | `frontend/src/Providers/RequestControls.ts`, mounted through `EspData.tsx` | The Turing web UI, still in the same frontend. Turing-only; not part of this contract. |
| `sensors` reply fields `type` (numeric), `self_update_enabled`, `hwConfig` | `frontend/src/Providers/NightMareClasses.ts` | The lost Turing rewrite's per-sensor hardware encoding. The backend ignores them; the frontend models them for Turing only. |
