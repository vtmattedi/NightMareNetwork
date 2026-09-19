# NightMare Network — Architecture Consolidation & Rewrite Brief

> **Purpose:** Implementation handoff for an agent performing the next major NightMare Network library and documentation rewrite.
>
> **Compatibility:** Backward compatibility is **not required**. Existing APIs, folders, includes, and internal mechanisms may be broken, replaced, merged, renamed, or moved to `Legacy/` where appropriate.
>
> **Primary goals:** Consolidate years of non-uniform evolution into a coherent architecture, reduce duplicated concepts, simplify dependencies, make network-visible capabilities first-class, keep embedded RAM/Flash costs predictable, and rewrite the public documentation around how **others can use NightMare Network**, not around one existing deployment.

---

## 1. Rewrite Principles

The rewrite is not a folder cleanup. It is an architectural consolidation.

The target codebase should follow a small number of consistent concepts:

1. **One network-facing resource model**
   - Sensors, information, writable state, actions, and events should share one coherent model.
   - Existing parallel mechanisms such as `ServerVariable`, ad-hoc controller exposure, sensor exposure, info exposure, and direct message-handler wiring should be consolidated.

2. **Register once, participate automatically**
   - A resource registered with the device should automatically participate in discovery, messaging, publication, remote access, reconnection behavior, and optional continuous telemetry.
   - Users should not manually wire a sensor/value into MQTT, `MessageHandler`, telemetry, and discovery separately.

3. **Separate model from runtime**
   - The resource model defines what a resource **is**.
   - A Resource Manager defines how a specific runtime reacts to it.
   - The model must be implementable by non-embedded/non-C++ clients such as backends, desktop applications, or other language runtimes.

4. **Separate semantics from transport**
   - Resources do not know MQTT topics.
   - MQTT is one transport representation of network messages.
   - Topic construction, routing, serialization, reconnect behavior, and subscriptions belong to Network/Transport/Manager layers.

5. **Keep embedded costs predictable**
   - Avoid unnecessary `String` instances, dynamic metadata, `std::function`, per-resource timers, per-resource tasks, and duplicated network machinery.
   - Primitive values may be duplicated when doing so simplifies code; saving 1–8 bytes of primitive state is less important than avoiding large per-resource framework overhead.

6. **Folder boundaries must mean something**
   - Folder location, include path, dependency direction, and documentation terminology should all represent the same architecture.

---

# 2. Network Topology

## 2.1 Network

The **NightMare Network** is the broad environment containing devices, clusters, brokers, bridges, backends, and eventually namespaces.

## 2.2 Cluster

A **Cluster** is a group of devices that communicate locally, normally through a Local MQTT broker.

The Local MQTT broker exists to provide low-latency device-to-device communication inside the cluster.

### Important invariant

> **The backend does not connect to Local MQTT.**

The local broker is not a low-latency path to the backend. It is a cluster-local coordination bus.

Expected topology:

```text
                     REMOTE / GLOBAL SIDE
                  ┌──────────────────────┐
                  │ Remote MQTT Broker   │
                  └──────────┬───────────┘
                             │
                        ┌────┴────┐
                        │ Backend │
                        └────┬────┘
                             │
                         Bridge(s)
                             │
        ┌────────────────────┴────────────────────┐
        │                                         │
   CLUSTER / SITE 1                          CLUSTER / SITE 2
┌─────────────────┐                      ┌─────────────────┐
│ Local MQTT      │                      │ Local MQTT      │
└───────┬─────────┘                      └───────┬─────────┘
        │                                        │
   ┌────┼────┐                              ┌────┼────┐
   │    │    │                              │    │    │
  D1   D2   D3                             D4   D5   D6
```

A cluster should be able to retain useful local behavior when the remote side is unavailable.

---

# 3. Reserved Architecture Topic: Namespaces

Namespaces should be accounted for in the rewrite now, but full namespace behavior should **not** expand the scope of this rewrite.

The objective is to avoid building APIs that assume `device + resource` is globally unique.

Conceptual identity should reserve:

```text
namespace / device / resource
```

The initial implementation may use a default namespace everywhere.

## 3.1 Keep Namespace and Cluster distinct

- **Cluster** = topology/local communication boundary.
- **Namespace** = logical naming/addressing/isolation boundary.

Do not hard-bind these concepts together.

Reserved future questions:

- Can one cluster contain multiple namespaces?
- Can one namespace span clusters?
- Are namespaces encoded directly in MQTT topics?
- Do bridges filter or map namespaces?
- Are namespaces security boundaries?
- Can devices belong to multiple namespaces?
- How does wildcard discovery work across namespaces?
- How does a default namespace behave?

Do not accidentally settle these questions while implementing the current rewrite.

---

# 4. Core Network Object Model

The following terms should become explicit architectural concepts.

## 4.1 Device

A **Device** is a participant implementing NM-NW.

It may own resources and consume resources owned by other participants.

A device is not necessarily only a physical sensor node. An ESP32, bridge, gateway, computer, UI device, or backend-like process may participate in the model depending on implementation.

## 4.2 Service

A **Service** is software behavior running on a device.

Examples:

- `LightController`
- OTA
- Climate control
- Serial resolver
- Power monitor

A Service is **not inherently the network API**.

A Service exposes network-visible behavior through Resources.

```text
Service
   ↓ exposes
Resources
```

A consumer should not need to know that `LightController` exists internally just to control a light.

---

# 5. Resource Model

## 5.1 Fundamental definition

> **Every network-visible capability of a NightMare device is represented as a Resource.**

A Resource is one of:

```text
NetResource
├── Value
├── Action
└── Event
```

Semantics:

```text
Value   = "what is the state?"
Action  = "please do this"
Event   = "this just happened"
```

Do not force all three through one generic "resource update" behavior.

---

# 6. NetResource Construction

Resource creation should be compact.

Do **not** store human-readable strings for things that can be represented as compact enums.

Use Arduino `String` for now. This rewrite is **not** the time to replace `String`.

The required string should normally be only the stable resource identifier.

Example conceptual enums:

```cpp
enum class NetResourceKind : uint8_t {
    VALUE,
    ACTION,
    EVENT
};

enum class NetValueType : uint8_t {
    BOOL,

    INT8,
    UINT8,
    INT16,
    UINT16,
    INT32,
    UINT32,
    INT64,
    UINT64,

    FLOAT32,
    FLOAT64,

    STRING
};

enum class NetAccess : uint8_t {
    READ,
    READ_WRITE
};
```

A pure `WRITE` Value is probably unnecessary initially. If something has no readable state and only receives an invocation, it is likely an Action.

### Type inference

The public typed API should infer kind and value type where possible.

Prefer:

```cpp
NetValue<float> temperature("temperature");
NetValue<uint8_t> brightness("brightness", NetAccess::READ_WRITE);
NetAction restart("restart");
NetEvent motionDetected("motionDetected");
```

Do not require:

```cpp
NetResource(
    "temperature",
    NetResourceKind::VALUE,
    NetValueType::FLOAT32,
    NetAccess::READ
);
```

for normal user code.

### Machine identity vs presentation

Use a stable protocol-facing `id`, not a display `name`.

```text
id    = "temperature"          // stable protocol identity
label = "Outdoor Temperature" // optional human-facing metadata
```

Human-readable metadata must be optional and should consume no memory unless explicitly supplied.

Avoid per-resource permanent strings such as:

- type string
- access string
- topic string
- namespace string
- description string
- path string

when the same information can be inferred or generated.

---

# 7. NetValue

`NetValue<T>` should be intentionally small and boring.

It should own its value by default.

Example:

```cpp
template<typename T>
class NetValue : public NetValueBase {
public:
    const T& get() const;
    bool set(const T& value);

private:
    T _value{};
};
```

The reason for owning primitive values is simplicity and predictable memory.

Duplicating a primitive value such as a `bool`, `uint8_t`, `int32_t`, or `float` is acceptable. The larger RAM risks are Strings, callbacks, metadata, registries, MQTT buffers, UI objects, JSON documents, and FreeRTOS task stacks.

Do not build getter/setter indirection or `std::function` into the core value model.

### Template/Flash constraint

Keep template-heavy code minimal.

Common behavior should live in non-template bases such as:

```text
NetResource
NetValueBase
```

Heavy serialization, MQTT, validation, routing, and discovery logic should not be duplicated through template instantiation.

---

# 8. Existing Concepts to Consolidate

Expected migration direction:

| Existing concept | Target model |
|---|---|
| `netSensor` | read-only `NetValue<T>` / optional convenience wrapper |
| `ServerVariable` | `NetValue<T>` |
| Info exposure | read-only `NetValue<T>` |
| Externally exposed controller property | writable `NetValue<T>` |
| Controller command | `NetAction` |
| Transient notification | `NetEvent` |
| `LightController` | Service exposing Resources |
| UI-specific control path | removed in favor of Resources |

`NetSensor<T>` may remain as a convenience type, but it must not remain a separate network mechanism.

`ServerVariable` should be phased out unless a uniquely useful behavior is found during implementation.

---

# 9. Resource Manager and Registry

Callbacks, runtime reactions, subscriptions, handlers, and synchronization do **not** belong inside Resources.

This is both an embedded optimization and a portability requirement.

A backend or other implementation should be able to consume the same resource model while implementing its own manager/event system.

## 9.1 Architectural split

Conceptually:

```text
Resource Model
    ↓
Resource Registry
    ↓
Resource Manager
    ↓
Network / Transport
```

Internally:

- **Registry** = what resources exist.
- **Manager** = makes them participate in the runtime/network.

The public API may expose them through one facade such as:

```cpp
network.resources()
```

but internal dependency boundaries should remain clean.

## 9.2 Register once, participate automatically

Example:

```cpp
NetValue<bool> door("door");

network.resources().add(door);
```

That registration should make the framework know:

- the device owns `door`
- its type
- its access
- its discovery/schema presence
- how incoming writes should be handled
- how current state is published
- how reconnect behavior works
- whether/when periodic telemetry occurs

The user should **not** manually wire:

```text
Resource → MQTT
Resource → MessageHandler
Resource → telemetry
Resource → discovery
```

separately.

---

# 10. Local vs Remote Resource Authority

Use one `NetValue<T>` model.

Do not create parallel conceptual families such as:

```text
NetValue
RemoteNetValue
LocalNetValue
RemoteSensor
```

The difference is in the binding/registration role.

Conceptually:

```cpp
enum class ResourceRole : uint8_t {
    LOCAL,
    REMOTE
};
```

### Local resource

The local device is authoritative.

```text
hardware / application
    ↓
manager.set(...)
    ↓
NetValue state changes
    ↓
publish authoritative state
```

### Remote resource

The local object mirrors state owned elsewhere.

```text
network update
    ↓
ResourceManager
    ↓
NetValue state changes
    ↓
manager-specific notification
```

A remote update must not be republished as authoritative state, avoiding echo loops.

### Access and role are independent

| Role | Access | Meaning |
|---|---|---|
| Local | Read | We publish state; others observe |
| Local | Read/Write | We publish state and accept write requests |
| Remote | Read | We mirror remote state |
| Remote | Read/Write | We mirror remote state and may send write requests |

### State vs write request

A remote request to change a writable Value is **not** automatically authoritative state.

Conceptually:

```text
remote WRITE request
    ↓
local manager handler
    ↓
service/application decides/applies
    ↓
manager.set(actual value)
    ↓
authoritative state publication
```

This supports validation, hardware failure, clamping, and rejected operations.

---

# 11. Resource Mutation Path

Prefer making normal network-aware mutation pass through the Resource Manager.

Conceptual distinction:

```cpp
value.set(x);
```

= change the local object only.

```cpp
resources.set(value, x);
```

= change authoritative managed state and trigger synchronization behavior.

This avoids storing a `ResourceManager*`, callback pointer, or observer pointer inside every resource.

The exact final API may differ, but preserve this architectural principle:

> A Resource must not need an embedded back-reference to the Manager in order to remain lightweight.

---

# 12. Publication Policies and Continuous Telemetry

Registration should support compact runtime policies.

Two important independent behaviors:

```text
ON_CHANGE
PERIODIC
```

Examples:

```text
door:
    publish on change
    optional long heartbeat

temperature:
    publish on change / threshold
    periodic telemetry

uptime:
    periodic

firmwareVersion:
    discovery / connect / change only
```

Do not give each resource its own timer.

Periodic resource publication should be driven centrally through Scheduler/Runtime.

Conceptual flow:

```text
Scheduler
    ↓
ResourceManager tick / due resources
    ↓
publish current authoritative state
```

Discovery/schema publication should be relatively infrequent and descriptive.

Telemetry/state messages should be compact.

---

# 13. Discovery and Device Schema

The Resource Registry effectively becomes the device's network schema.

Example:

```text
device A
├── door
│   kind: VALUE
│   type: BOOL
│   access: READ
│
├── temperature
│   kind: VALUE
│   type: FLOAT32
│   access: READ
│
├── brightness
│   kind: VALUE
│   type: UINT8
│   access: READ_WRITE
│
└── restart
    kind: ACTION
```

Generic UIs, backends, other devices, or future agents should be able to learn a device's interface from this schema without knowing its firmware implementation.

Schema/discovery may be more verbose.

Frequent telemetry must not repeat schema fields unnecessarily.

---

# 14. Message Semantics

The resource protocol should expose distinct operations, not one generic update message.

Fundamental message vocabulary:

```text
VALUE_STATE
VALUE_WRITE

ACTION_INVOKE
ACTION_RESULT

EVENT_EMIT
```

If the topic/address already identifies namespace/device/resource/operation, do not repeat all of those fields in every payload.

Avoid unnecessarily verbose messages such as:

```json
{
  "device": "deviceA",
  "resource": "temperature",
  "type": "float",
  "operation": "state",
  "value": 24.5
}
```

when discovery already defines the schema.

A compact state message may simply carry:

```text
24.5
```

---

# 15. MessageHandler / Dispatcher

The existing `xtra/MessageHandler` is conceptually central to NM-NW and should be promoted into the main Network architecture.

Expected flow:

```text
Transport
   ↓
MessageDecoder
   ↓
MessageHandler / Dispatcher
   ↓
operation type
   ├── VALUE_STATE
   ├── VALUE_WRITE
   ├── ACTION_INVOKE
   ├── ACTION_RESULT
   └── EVENT_EMIT
          ↓
ResourceManager
```

The dispatcher must not know about application classes such as `LightController`.

It routes protocol messages to generic resources/managers.

The exact class name (`MessageHandler`, `Dispatcher`, etc.) may be refined, but it should no longer live in an `xtra`/miscellaneous area.

---

# 16. Actions

An Action is a directed request.

Actions should support:

```text
no arguments
one scalar argument
structured arguments
```

Conceptually:

```cpp
NetAction<void> restart("restart");
NetAction<uint8_t> setBrightness("setBrightness");
NetAction<SetColorArgs> setColor("setColor");
```

The model may later support typed results:

```cpp
NetAction<Args, Result>
```

## 16.1 Structured input

Do not use arbitrary command strings as the canonical action argument mechanism.

Example:

```cpp
struct SetColorArgs {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};
```

The network schema should know:

```text
setColor
kind: ACTION
input:
    r: UINT8
    g: UINT8
    b: UINT8
```

Wire encoding can remain compact and need not repeat field names if the schema already defines ordering/types.

## 16.2 Response policy

Actions may require different response levels.

Conceptually:

```cpp
enum class ActionResponse : uint8_t {
    NONE,
    ACK,
    RESULT
};
```

### NONE

Fire-and-forget.

Useful for things such as:

- toggle
- identify
- beep
- refresh

No request ID or response overhead is needed.

### ACK

The caller needs to know whether the action was accepted/executed.

A compact status enum may be used:

```cpp
enum class ActionStatus : uint8_t {
    OK,
    REJECTED,
    INVALID_ARGUMENT,
    BUSY,
    ERROR
};
```

A small correlation/request ID should only be included when a response is required.

### RESULT

Used when the action returns structured data.

Do not use globally unique string UUIDs unless truly required. A compact correlation ID is preferable.

## 16.3 Long-running work

An Action does not need to stay pending for long-running operations.

Example:

```text
ACTION_INVOKE runTest
    ↓
ACK STARTED
    ↓
later
EVENT testFinished
```

This is preferable to keeping a request pending for long periods.

---

# 17. Events

An Event is a transient occurrence.

Examples:

```text
buttonPressed
motionDetected
alarmTriggered
connectionLost
```

Rules:

- Events are not retained as state.
- Events do not have a "current value".
- Events normally have no application-level acknowledgement.
- Offline consumers normally miss them.
- If something must remain knowable later, expose corresponding Value state.

Example:

```text
doorOpened = Event
doorOpen   = Value<bool>
```

Event payload may be absent:

```text
buttonPressed → empty payload
```

or typed:

```text
alarm → UINT8 payload
```

Do not attach timestamp, sequence number, source strings, or diagnostic data to every event by default.

Those can become optional when semantics require them.

MQTT QoS is a transport concern and should not redefine Event semantics.

---

# 18. Console Integration

Do **not** make `console/in` the canonical Action protocol.

Console should become an ingress adapter into the same action/message model used by MQTT and other transports.

Conceptual flow:

```text
MQTT action message ─┐
                     │
Console command ─────┼──> MessageHandler / Dispatcher
                     │              ↓
Other transport ─────┘        ResourceManager
                                    ↓
                                NetAction
```

If an Action exists:

```cpp
NetAction<SetColorArgs> setColor("setColor");
```

the Console may expose a human-friendly representation such as:

```text
setColor 255 120 0
```

or:

```text
setColor r=255 g=120 b=0
```

but internally it should produce the same typed `ACTION_INVOKE`.

The Resource Registry should be reusable by Console, MQTT, UI, Backend, and future agent-facing interfaces rather than maintaining parallel command definitions.

---

# 19. Services and Controllers

Controllers are implementation behavior, not a separate network resource type.

Example:

```text
LightController
      ↓
exposes:
    light/enabled
    light/brightness
    light/state
    light/toggle
```

A UI or remote participant consumes these resources without needing to know the internal controller class.

The rewrite should remove dedicated "UI → Controller" network paths where they can be represented as Resources.

---

# 20. Scheduler, Timers, Jobs, and Runtime

## 20.1 Merge Timer semantics into Scheduler

Timers and Scheduler are strong candidates for consolidation.

Use Scheduler as the central time-based execution abstraction.

Conceptually support:

```cpp
scheduler.after(...);
scheduler.every(...);
scheduler.dailyAt(...);
```

Do not keep parallel Timer and Scheduler conceptual systems unless a concrete implementation requirement demands it.

## 20.2 Use "Job", not "Task", for scheduled logical work

Reserve **Task** for runtime/FreeRTOS execution.

Use **Job** for logical scheduled behavior.

Example:

```text
AC.shutdown
    = Action

05:00 daily
    = Schedule

"shutdown AC at 05:00"
    = Job
```

This avoids confusing statements about "Scheduler tasks running in Tasks".

## 20.3 Jobs are higher-level behavior, not Resource kinds

"Shut down the AC every day at 05:00" is not itself a Resource.

It is behavior composed from an Action and a Schedule.

Conceptually:

```text
Resource Model
    AC.shutdown

        ↑ used by

Job / Automation
    daily-ac-shutdown

        ↑ executed by

Scheduler

        ↑ driven by

Runtime
```

## 20.4 Job Manager may expose resources

Jobs may be remotely manageable, but this does not require a fourth Resource kind.

A Job subsystem may expose ordinary Resources such as:

```text
automation/ac-shutdown/enabled   VALUE<bool> R/W
automation/ac-shutdown/nextRun   VALUE<time> R
automation/ac-shutdown/lastRun   VALUE<time> R
automation/ac-shutdown/runNow    ACTION
```

Potentially also schedule parameters if they are user-configurable.

This lets the Scheduler/Job subsystem participate in the same network resource model.

---

# 21. Runtime and FreeRTOS Tasks

Do not let every subsystem independently create its own task by default.

Avoid:

```text
Scheduler → task
SerialResolver → task
Network → task
Service A → task
Service B → task
```

Components should be fundamentally pollable/driveable:

```text
scheduler.tick()
serial.tick()
network.tick()
resourceManager.tick()
```

A centralized Runtime decides who drives them.

Support at least two execution modes conceptually:

```text
Manual mode
    application calls nightmare.loop()

Managed mode
    NightMare Runtime drives registered components
    from one or more managed FreeRTOS tasks
```

Do not encode execution policy inside Scheduler or SerialResolver themselves.

This centralizes decisions around:

- task count
- stack size
- priority
- core affinity
- blocking rules
- synchronization
- memory usage

Start with minimal task proliferation.

A future multi-executor/multi-task model may exist, but do not default to one task per subsystem.

---

# 22. Serial Resolver

Serial Resolver should follow the same runtime principle.

It may be polled manually or driven by the Runtime.

Do not make a dedicated Serial task mandatory.

If later there is a strong reason for a dedicated task, that should be an execution policy configured through Runtime rather than hard-coded into Serial Resolver.

---

# 23. Time Abstraction

NightMare currently uses TimeLib.

The rewrite should move to normal/internal system time while preserving a small TimeLib-like adaptation API.

## 23.1 Canonical clock

> **NightMare uses the platform/system epoch clock as its canonical wall clock. Time synchronization is separate.**

Provide a thin API such as:

```cpp
namespace NMTime {

using timestamp_t = time_t;

timestamp_t now();

uint8_t second(timestamp_t time = now());
uint8_t minute(timestamp_t time = now());
uint8_t hour(timestamp_t time = now());

uint8_t day(timestamp_t time = now());
uint8_t month(timestamp_t time = now());
uint16_t year(timestamp_t time = now());

bool isValid();
void set(timestamp_t time);

}
```

Exact names may be adjusted, but preserve the familiar call pattern:

```text
now()
second()
minute()
hour()
day()
month()
year()
```

Prefer `time_t` internally rather than baking `unsigned long` into the architecture.

## 23.2 Current synchronization strategy remains

For now preserve the existing synchronization flow:

```text
Control/Request
      ↓
 Control/Time
      ↓
 NMTime::set(...)
      ↓
 system clock
```

Proper NTP support is deferred.

Later NTP should become another time source:

```text
NTP
 ↓
NMTime::set(...)
```

without changing Scheduler/Jobs.

## 23.3 Valid time

Expose whether wall-clock time is valid.

Calendar Jobs must not execute against an unsynchronized/default epoch.

Example:

```text
boot
  ↓
wall clock invalid
  ↓
daily 05:00 Jobs wait

Control/Time sync
  ↓
NMTime valid
  ↓
calendar Jobs become eligible
```

## 23.4 Monotonic vs wall-clock time

Do not replace all uses of `millis()` with wall-clock time.

Use:

```text
Monotonic time
    delays
    intervals
    timeouts
    "every N seconds"

Wall-clock time
    daily 05:00
    calendar dates
    timestamps
```

Wall-clock synchronization may jump forward/backward and must not break interval timers.

## 23.5 Reserved Topic: Timezone/DST

Do not implement a large timezone system in this rewrite.

Reserve a future architecture topic:

**Timezone & DST**

For now, calendar Jobs should use the configured local system time.

Do not permanently define `dailyAt(05:00)` as UTC simply for implementation convenience.

---

# 24. OTA

`Core/OTA` is likely misplaced.

OTA is important, but importance does not mean "Core".

OTA is more naturally a Service:

```text
Services/
    System/
        OTA/
```

Platform-specific flash/update implementation may live behind a Platform adapter if useful.

Core must not depend on networking, HTTP, flash, filesystem, or other heavy subsystems merely because OTA was historically located there.

---

# 25. TCP → Legacy

Move the TCP module to:

```text
Legacy/TCP/
```

unless implementation inspection reveals a better legacy grouping.

Documentation should explicitly explain:

> The TCP module predates the current NightMare Network architecture and was the predecessor from which NM-NW evolved. It remains under Legacy for reference or existing implementations, but is not part of the current network model.

Do not include Legacy automatically in the default umbrella include.

---

# 26. Folder / Dependency Architecture

Use strict conceptual ownership.

A starting target:

```text
Core/
    fundamental NightMare concepts/types
    identity
    lifecycle
    time abstraction
    basic configuration primitives

Util/
    generic helpers that do not know NightMare Network exists

Runtime/
    execution policy
    loop/task driver
    Scheduler
    Jobs
    task configuration

Network/
    protocol/message model
    MessageHandler / Dispatcher
    addressing
    discovery
    MQTT transport
    bridge-related protocol behavior

Resources/
    NetResource
    NetValue
    NetAction
    NetEvent
    ResourceRegistry
    ResourceManager interfaces/model

Services/
    reusable device capabilities
    LightController
    OTA
    monitoring
    etc.

Platform/
    ESP32/Arduino/FreeRTOS-specific adapters when isolation is useful

Legacy/
    predecessor or deprecated implementations
    TCP
```

`Resources/` may ultimately become `Network/Resources/` if that better matches implementation ownership, but preserve the conceptual distinction.

## 26.1 Dependency rules

Examples:

> If a class knows MQTT exists, it is not `Core`.

> If removing NightMare-specific types makes the class generally reusable, it is probably `Util`.

> If it implements device/application behavior, it is probably a `Service`.

> If it defines how another network participant sees/interacts with the device, it belongs to the Resource/Network model.

> Avoid catch-all folders such as `xtra`, `misc`, etc. Components must have explicit architectural ownership.

---

# 27. Includes and Public API

Folder path, include path, and conceptual ownership should match.

Example target style:

```cpp
#include <NightMare/Core/Time.h>
#include <NightMare/Network/Network.h>
#include <NightMare/Resources/NetValue.h>
#include <NightMare/Services/LightController.h>
#include <NightMare/Runtime/Scheduler.h>
```

Keep:

```cpp
#include <NightMare.h>
```

as a convenience umbrella where appropriate.

Implementation-only APIs should move behind something like:

```text
detail/
internal/
```

Legacy should not be silently pulled in by the default umbrella include.

---

# 28. Resource Metadata

Metadata should be optional.

Potential metadata:

```text
label
unit
min
max
description
category/group
```

Do not make every Resource permanently own multiple `String`s.

The underlying model should allow generic consumers to learn enough information to render/control a resource.

Example conceptual discovery:

```text
id: brightness
kind: VALUE
type: UINT8
access: READ_WRITE
min: 0
max: 100
unit: percent
label: Brightness
```

This allows a generic UI to render a slider without knowing `LightController`.

However, metadata must remain an optional cost.

---

# 29. Resource Grouping / Hierarchy — Open Design Item

Do not prematurely hard-code hierarchical IDs such as:

```text
light/brightness
```

as the only internal identity model.

Possible future distinction:

```text
resource id: brightness
group/service/category: light
```

Transport may render a hierarchy such as:

```text
namespace/device/light/brightness
```

but grouping and identity should be reviewed before being permanently coupled.

This is an **open implementation decision**.

---

# 30. MQTT Mapping — Principles, Not Final Topic Syntax

Do not lock the rewrite to one topic layout before the resource/address model is stable.

Principles:

- Resources do not store MQTT topic strings.
- Topic paths are derived by transport from ResourceAddress/model.
- Schema/discovery messages may be verbose.
- High-frequency state/event messages should be compact.
- Use wildcard subscriptions where practical instead of manually managing one application subscription per resource.
- A Resource Manager/Dispatcher resolves messages to registered resources.
- Local vs Remote MQTT routing/bridge behavior must respect cluster topology.
- Namespace must remain reservable in addressing.

Action replies may benefit from one generic reply endpoint per participant instead of one reply topic per Action.

Exact topic syntax remains an implementation design item.

---

# 31. Reconnection Behavior

Resources should not need to re-register manually after transport reconnection.

Conceptual behavior:

```text
MQTT reconnect
    ↓
ResourceManager
    ↓
re-announce schema/device if needed
    ↓
republish current authoritative state as required
    ↓
resume periodic telemetry
```

Reconnection is framework behavior, not application behavior.

---

# 32. Documentation Rewrite

The documentation must change both **structure** and **tone**.

## 32.1 New narrative order

Prefer:

```text
Idea / Why NightMare Network exists
    ↓
Network topology
    ↓
Minimum topology
    ↓
Expected local/remote topology
    ↓
Clusters / bridges / backend roles
    ↓
Resource model
    ↓
Messaging / discovery
    ↓
Runtime / scheduler / jobs
    ↓
Services / modules
    ↓
Examples
    ↓
API reference
```

Avoid beginning the documentation with individual classes.

## 32.2 Minimum topology

Explain the smallest usable network clearly.

Example:

```text
        MQTT broker
             │
      ┌──────┴──────┐
      │             │
   Device A      Device B
    NM-NW         NM-NW
```

Then expand to expected cluster/remote topology.

## 32.3 Remove deployment-specific components from the architecture

Do not describe undefined historical components such as **"The Dashboard"** as though they are special protocol entities.

A dashboard is simply an application/device/network participant consuming NM-NW resources.

Likewise, avoid documentation phrasing that assumes one MattediWorks deployment is the architecture.

## 32.4 Change tone

Replace:

> "This is how we use LightController from the Dashboard."

with generalized language such as:

> "A device can expose a controllable light through the NightMare resource model. Any authorized NM-NW participant can discover and interact with the resources the device exposes."

Then optionally explain that `LightController` is one implementation of the pattern.

The docs should teach:

> "This is how you/one can use NightMare Network."

not:

> "This is how our deployment happens to use it."

## 32.5 Historical context

Document how the network evolved when that context clarifies architecture.

For example, TCP should be explained as the predecessor of the current NM-NW architecture rather than simply being marked obsolete.

---

# 33. Documentation Structure Should Mirror Code Structure

Code and docs should use the same vocabulary.

If the code defines:

```text
Core
Runtime
Network
Resources
Services
Platform
Legacy
```

the documentation should explain those same concepts and their dependency boundaries.

Avoid docs taxonomy that does not match the code taxonomy.

---

# 34. Migration Expectations

The implementation agent should inspect the existing codebase and map historical mechanisms into the new model.

At minimum:

1. Inventory:
   - `ServerVariable`
   - `netSensor`
   - Sensors/data/info exposure
   - Controllers and UI control paths
   - `LightController`
   - `MessageHandler`
   - `console/in`
   - MQTT subscriptions/publication
   - continuous telemetry
   - Scheduler
   - Timers
   - task creation
   - Serial Resolver
   - OTA
   - TCP
   - Control/Request and Control/Time
   - existing TimeLib usage

2. Identify duplicated responsibilities.

3. Create the new Resource model and Manager path.

4. Migrate representative modules first:
   - one read-only sensor/value
   - one read/write controller value
   - one Action
   - one Event
   - one Info-like resource
   - one scheduled Job

5. Remove manual duplicate wiring after the manager path is proven.

6. Consolidate Scheduler/Timers and runtime execution.

7. Introduce the Time adapter and migrate TimeLib consumers.

8. Move/rename folders and includes after conceptual ownership is stable.

9. Move TCP to Legacy.

10. Move OTA out of Core.

11. Rewrite docs after the architecture/API is stable enough that documentation reflects the target model, not intermediate states.

---

# 35. Implementation Priorities

Prefer this order:

## Phase 1 — Architecture inventory
- inspect current dependencies and behavior
- identify current API contracts
- identify where state/messages are duplicated

## Phase 2 — Resource foundation
- `NetResource`
- `NetValue`
- `NetAction`
- `NetEvent`
- compact enums/type mapping
- address structure with namespace reservation

## Phase 3 — Registry/Manager
- local and remote roles
- registration
- state mutation path
- write requests
- discovery/schema
- publication policy
- reconnect behavior

## Phase 4 — Messaging
- promote/refactor MessageHandler into Network dispatcher
- implement distinct message semantics
- route Value/Action/Event operations
- adapt MQTT
- adapt Console into the same Action path

## Phase 5 — Services migration
- migrate sensors/info
- migrate controller exposure
- migrate `LightController`
- deprecate/remove `ServerVariable`

## Phase 6 — Runtime
- merge Timer/Scheduler concepts
- introduce Jobs
- centralize runtime execution
- support manual vs managed loop/task policy
- migrate Serial Resolver execution

## Phase 7 — Time
- internal system time adapter
- keep Control/Request → Control/Time synchronization
- remove direct TimeLib dependency from higher layers
- preserve wall-clock validity handling
- leave NTP for later

## Phase 8 — Structure
- normalize folders/includes
- move OTA
- move TCP to Legacy
- remove `xtra`/misc ownership where possible

## Phase 9 — Documentation
- rewrite conceptual introduction
- topology
- resource model
- runtime
- modules
- examples
- reference

---

# 36. Non-Goals for This Rewrite

Unless required by implementation discoveries, do **not** expand this rewrite into:

- full namespace routing/isolation
- namespace ACL/security model
- NTP implementation
- advanced timezone database/DST framework
- generalized distributed consensus/shared resource ownership
- one task per module
- arbitrary dynamic reflection system
- heavy dynamic metadata on every embedded resource
- full RPC framework beyond the Action needs defined here
- strict backward compatibility
- maintaining historical folder names only to avoid breakage

---

# 37. Reserved Future Topics

Create explicit follow-up architecture topics for:

1. **Namespaces & multi-cluster isolation**
2. **Timezone & DST**
3. **NTP / multiple time sources**
4. **Resource grouping/hierarchy**
5. **Advanced Action results / async result semantics**
6. **Security / authorization**
7. **Bridge filtering and namespace mapping**
8. **Persistent Job configuration**
9. **Multiple Runtime executors / task affinity**
10. **Resource metadata richness vs embedded memory budget**

Do not silently solve these inside unrelated modules.

---

# 38. Acceptance Criteria

The rewrite should be considered successful when:

### Resource model
- A read-only sensor can be registered once and automatically discovered/published.
- A read/write value can be registered once and receive remote write requests through the generic path.
- An Action can be invoked locally/remotely with typed arguments.
- An Event can be emitted without becoming persistent state.
- `ServerVariable` is no longer required for new code.
- Controllers/Services expose Resources instead of requiring special UI paths.

### Resource Manager
- Resources do not store MQTT topics or runtime callbacks.
- Manager/Registry owns runtime behavior.
- Local vs remote authority is explicit.
- Remote mirror updates do not echo back as authoritative publication.
- Reconnect does not require application re-registration.

### Messaging
- `MessageHandler`/Dispatcher is part of Network architecture, not `xtra`.
- Console and MQTT ultimately route Actions through the same internal semantics.
- Frequent messages do not repeat unnecessary schema strings.

### Scheduler/Runtime
- Timer/Scheduler duplication is reduced or eliminated.
- Logical scheduled work is called a **Job**.
- Calendar Jobs and interval Jobs use the proper clock source.
- Runtime may be manually loop-driven or managed without forcing each subsystem to own a task.
- Serial Resolver is compatible with the same runtime policy.

### Time
- Higher-level code no longer depends directly on TimeLib behavior.
- `now()/second()/minute()/hour()/day()/month()/year()`-style helpers remain available through the new adapter.
- `Control/Request → Control/Time` still synchronizes the system clock.
- Calendar Jobs wait until wall-clock time is valid.

### Structure
- OTA is no longer incorrectly treated as Core.
- TCP is moved to Legacy and documented as the predecessor of NM-NW.
- Folder/include structure matches architectural ownership.
- `xtra`/miscellaneous ownership is reduced or eliminated.

### Documentation
- Docs start from the idea and topology, not from classes.
- Local MQTT is clearly described as cluster-local and backend-independent.
- Backend is shown only on the remote/global side.
- Undefined special entities such as "The Dashboard" are removed from architectural descriptions.
- Tone is generalized toward how a user can build with NightMare Network.
- Code and docs share the same vocabulary.

---

# 39. Architectural Statements to Preserve

The following statements summarize the intended design and should remain true unless an implementation blocker is documented:

> **Register once, participate automatically.**

> **Every network-visible capability is represented as a Resource.**

> **Values are state, Actions are directed requests, Events are transient occurrences.**

> **Services implement behavior; Resources are what the network sees.**

> **The Resource model is transport-agnostic and runtime-agnostic.**

> **Callbacks, handlers, subscriptions, and synchronization belong to the Manager, not the Resource.**

> **Resources do not know MQTT topics.**

> **The Resource Registry is the device's network schema.**

> **Local MQTT is a cluster-local coordination bus. The backend does not connect to it.**

> **Namespace and Cluster are separate concepts.**

> **Scheduled behavior is a Job, not a Resource and not a FreeRTOS Task.**

> **Scheduler decides when; an Action/handler defines what; Runtime decides how execution is driven.**

> **Use monotonic time for durations and wall-clock time for calendar Jobs.**

> **Backward compatibility is not a constraint for this rewrite.**

---

# 40. Agent Guidance

Before changing APIs aggressively, inspect the actual current implementations and identify hidden behaviors that may need to survive under the new architecture.

Do not preserve old abstractions merely because they exist.

When an existing module does not fit the target architecture:

1. identify the behavior that is still useful,
2. move that behavior into the correct new layer,
3. migrate callers,
4. remove or move the historical implementation to `Legacy/` if it remains useful for reference.

Prefer fewer, stronger abstractions over wrappers around every old concept.

When uncertain, prioritize:

1. architectural consistency,
2. predictable embedded RAM/Flash use,
3. code legibility,
4. portability of the network model,
5. ease of generic discovery/consumption,
6. backward compatibility only if it costs essentially nothing.

