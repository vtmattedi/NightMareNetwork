# NightMare Network — Website, Documentation & MCP Rewrite Brief

> **Purpose:** Post-architecture implementation handoff for rewriting the public NightMare Network website, documentation, and MCP surface after the library consolidation/rewrite is complete.
>
> **Timing:** This work happens **after** the library architecture/resource/runtime refactor is complete. The refactor phase must intentionally **not modify the public website or MCP**. This document defines the follow-up phase that updates both surfaces to reflect the finished architecture and APIs.
>
> **Primary goal:** Reframe NightMare Network from documentation of one internal deployment into a reusable distributed device/network framework that others can understand, adopt, extend, and integrate.
>
> **Important surface split:** The public website has two distinct stages/surfaces:
>
> 1. **Story / positioning / evolution** — why NightMare Network exists, how it evolved, what problems it solves, and what it is capable of.
> 2. **Technical documentation** — concepts, architecture, usage, examples, migration, and API/reference material.
>
> **MCP exposes only the technical documentation/source/reference surface.** It is not intended to reproduce the storytelling/marketing layer.

---


# 0. Delivery Sequence

This document is explicitly a **second-phase handoff**.

## Phase A — Library refactor

The architecture/resource/runtime refactor is performed first.

During that phase:

- do not rewrite the public website
- do not restructure the MCP documentation index
- do not update public narrative pages merely to match temporary intermediate APIs
- do not let website/MCP compatibility constrain the internal rewrite

The refactor should finish with the library architecture and public APIs in their intended new form.

## Phase B — Website / docs / MCP alignment

Only after the refactor is complete:

1. inspect the final code structure and API
2. update the technical docs to match it
3. rewrite the website storytelling/positioning layer
4. update examples and migration guides
5. update MCP indexing/content so it reflects the new docs/source structure
6. verify that MCP does not surface removed/Legacy APIs as current

The website/docs/MCP pass should describe the **finished refactor**, not preserve the pre-refactor model.

---

# 0.1 Website Has Two Distinct Layers

The website should deliberately separate:

```text
Website
├── Story / Why / Evolution / Capabilities
└── Documentation
    ├── Concepts
    ├── Architecture
    ├── Usage
    ├── Examples
    ├── Migration
    └── API / Reference
```

These layers may share navigation and visual design, but they have different purposes.

## Story / positioning layer

This answers questions such as:

- Why was NightMare Network created?
- What problem did the original TCP implementation solve?
- Why did it move toward MQTT?
- Why were common message handling, request/response, ServerVariables, timers, Scheduler, Resources, and the Registry added?
- What kinds of systems can someone build with it today?
- What makes the architecture useful without requiring a centralized application?

This layer may use narrative, diagrams, evolution timelines, and capability-oriented explanations.

## Documentation layer

This answers:

- How does the current architecture work?
- How do I use it?
- What are Value, Action, Event, Service, Job, Runtime, Cluster, Bridge, etc.?
- How do I register resources?
- How do I build a cluster?
- How do I use local/remote MQTT?
- How do I migrate from old APIs?
- What are the actual current classes/functions/includes?

The docs should be technical, precise, current, and implementation-aligned.

## MCP boundary

MCP should index/expose the **Documentation layer**, plus source/examples/reference/version information.

MCP should not need to expose the Story/positioning pages except where a conceptual overview is useful as technical context.

The MCP is an agent-facing documentation/source interface, not a mirror of the entire public website.

---

# 1. Core Narrative

The public documentation should explain NightMare Network as an evolving distributed device/network framework.

The story should begin with the problem it originally solved and show how that problem expanded.

## 1.1 Origin

NightMare Network began from a much smaller need:

> Enable one device to directly trigger an action on another device.

The first implementation used a custom TCP client/server model.

Conceptually:

```text
Device A
   │
   │ custom TCP
   ↓
Device B
```

This was effectively point-to-point communication.

It solved the immediate problem, but it tied communication strongly to specific device pairs and a more centralized/direct topology.

## 1.2 Evolution toward a network

As the number of devices and interactions grew, point-to-point communication became too rigid.

NightMare evolved toward a message-based network using MQTT.

This changed the model from:

```text
Device A → Device B
```

to:

```text
Device A
   ↓
 MQTT Broker
   ↑
Device B
```

MQTT still requires a broker, but devices no longer need to know how to directly establish application-specific connections to every other device.

The network became less centralized in its **application behavior**, even though MQTT itself still uses broker infrastructure.

The documentation should be careful here:

> NightMare Network does not claim to be serverless or brokerless.

Instead:

> It reduces direct coupling between devices and allows behavior to be distributed across devices, clusters, bridges, and backend services.

---

# 2. Current Architectural Position

The current NightMare Network should be presented as supporting both:

- distributed device-to-device behavior
- centralized/backend-assisted behavior

Neither should be described as the only correct architecture.

A deployment may use NightMare Network primarily as:

```text
Device ↔ Device
```

or:

```text
Devices ↔ Backend
```

or both:

```text
                Remote MQTT
                     │
                  Backend
                     │
                   Bridge
                     │
                 Local MQTT
                 /    |    \
             Device Device Device
```

The important distinction:

> The backend is an optional higher-level participant in the architecture, not the defining center of the protocol.

NightMare Network should continue to support useful local behavior inside a device cluster even when the backend or remote side is unavailable.

---

# 3. Local MQTT vs Remote MQTT

The docs must clearly explain the intended topology.

## Local MQTT

A Local MQTT broker belongs to a cluster of nearby/logically grouped devices.

Its purpose is:

- low latency
- device-to-device interaction
- local resilience
- avoiding unnecessary remote network roundtrips

The backend does **not** connect directly to Local MQTT.

## Remote MQTT

Remote MQTT connects:

- bridges
- backend services
- remote participants
- wider network coordination

The bridge is the boundary between local cluster communication and remote/global communication.

Do not present Local MQTT merely as a faster route to the backend.

---

# 4. Reserved Namespace Concept

The website/docs should acknowledge that NightMare Network is designed to eventually support multiple logical device groups or installations on the same wider network.

Examples:

```text
home-1
home-2
lab
greenhouse
factory-floor
```

Use the language:

```text
Namespace = logical addressing/isolation scope
Cluster   = local communication/topology scope
```

Do not claim that namespace behavior is fully implemented unless it actually is.

If namespace support is not complete, place it under:

```text
Architecture / Planned or Reserved Concepts
```

rather than mixing future semantics into current usage instructions.

---

# 5. Explain the Evolution of Capabilities

NightMare Network did not begin as a large framework.

It accumulated common device/network patterns as they repeatedly appeared in real projects.

The documentation should explain this evolution because it helps users understand *why* the library contains certain modules.

A useful narrative is:

```text
Point-to-point TCP
    ↓
Common message handling
    ↓
MQTT transport
    ↓
Request/response over MQTT
    ↓
Reusable network-visible state
    ↓
ServerVariables
    ↓
Software timers / setTimeout-like behavior
    ↓
Scheduler
    ↓
Controllers / Sensors / Info exposure
    ↓
Net Resources
    ↓
Automatic Resource Registry
```

This should not be written as a historical changelog.

Instead, use the history to explain the motivation behind the current architecture.

Example:

> Repeated projects needed the same patterns: receive commands from multiple sources, expose state, schedule work, request responses, publish telemetry, and allow other devices or applications to discover capabilities. NightMare Network gradually standardized these recurring patterns into reusable network primitives.

---

# 6. Message Handling as a Unifying Layer

One important historical/conceptual development is the move toward common message handling regardless of where a message originates.

The docs should explain that commands may originate from:

- MQTT
- Console
- internal services
- future transports/adapters

but should converge into a common dispatch model.

Conceptual flow:

```text
MQTT ──────────┐
               │
Console ───────┼──> Message Dispatcher
               │          ↓
Other source ──┘     Resource Manager
                          ↓
                    Device behavior
```

Avoid documentation that makes each transport look like a separate application architecture.

Transports are ingress/egress mechanisms.

The resource/message model is the shared semantic layer.

---

# 7. Request/Response over MQTT

The docs should explain that MQTT is used not only for broadcast-style telemetry but also for directed request/response interactions where needed.

Examples:

- request information
- invoke an Action and wait for acknowledgement
- invoke an Action and receive a typed result
- control a writable Value
- synchronize time
- request discovery/schema information

Explain the distinction between:

```text
fire-and-forget
acknowledged request
request with result
```

Do not force all MQTT interactions into one model.

---

# 8. Resource Model

The new resource model should become one of the central conceptual sections of the site.

Define:

```text
Value   = current state
Action  = directed request
Event   = transient occurrence
```

Examples:

```text
temperature       Value<float>
brightness        Value<uint8_t> read/write
restart           Action
buttonPressed     Event
```

Explain that older ideas such as:

- ServerVariables
- exposed Sensors
- Info
- controller properties

were progressively consolidated into a common resource model.

The docs should emphasize:

> A Resource describes what another participant can observe or interact with.

A Service or Controller is the implementation behind those Resources.

---

# 9. Automatic Resource Registry

The Resource Registry should be presented as a major current capability.

Users should understand that the modern model is:

> Register a Resource once and allow the framework to integrate it into the network.

Example:

```cpp
NetValue<bool> door("door");

network.resources().add(door);
```

The docs should explain that registration can drive:

- discovery
- message routing
- publication
- remote access
- reconnect behavior
- telemetry policy
- generic UI/backend consumption

Avoid examples where the user manually duplicates registration into:

```text
MQTT
MessageHandler
Telemetry
Discovery
```

unless the example is specifically demonstrating low-level internals.

---

# 10. Resource Discovery and Generic Consumers

The docs should explain that registered resources form the device's network-visible schema.

Example:

```text
Device: hallway-controller

Resources:
  door
    kind: VALUE
    type: BOOL
    access: READ

  temperature
    kind: VALUE
    type: FLOAT32
    access: READ

  brightness
    kind: VALUE
    type: UINT8
    access: READ_WRITE

  identify
    kind: ACTION
```

This enables consumers such as:

- another device
- backend service
- UI
- CLI/Console
- agent/tooling integration

to understand available capabilities without needing to know the internal C++ classes.

---

# 11. Do Not Turn the Docs into "The Dashboard"

Remove language that implies undefined deployment-specific components are part of NightMare Network architecture.

For example, avoid treating:

```text
"The Dashboard"
```

as a protocol-level entity unless an actual standardized Dashboard component exists.

Instead:

> A dashboard is one possible NM-NW consumer that discovers and interacts with device Resources.

Likewise, avoid implying that a backend, UI, controller, or specific MattediWorks service is required unless it truly is.

---

# 12. Tone Change

The current documentation should move from:

> "This is how we use it."

to:

> "This is what NightMare Network can do and how you can use it."

Examples should be generalized.

Instead of:

> We use `LightController` to control our light from the Dashboard.

Prefer:

> A device can expose a light as writable state and Actions. Any NM-NW participant can discover and control those Resources.

Then:

> `LightController` is one implementation that provides this pattern.

The documentation should teach a reusable framework, not reconstruct one existing deployment.

---

# 13. Website Information Architecture

The website should visibly support the two-stage experience:

```text
Home / Story
├── Why NightMare Network
├── Evolution
├── Capabilities
├── Architecture at a glance
└── Enter the Docs

Docs
├── Getting Started
├── Concepts
├── Network Architecture
├── Resources
├── Messaging
├── Runtime & Scheduling
├── Services
├── Examples
├── Reference
├── Migration / Legacy
└── MCP
```

## Home / Story

This should be the narrative entry point.

Answer:

- Why was NightMare Network created?
- What problem did the original TCP model solve?
- How did that grow into MQTT and a wider network?
- Why were common message handling, request/response, ServerVariables, timers/Scheduler, and Resources added?
- What is NightMare Network capable of today?
- What kinds of topologies can it support?
- Why is the framework not inherently backend-centralized?
- Where should a user go next to actually implement something?

The Home/Story layer should lead naturally into the Docs.

## Getting Started

Provide the fastest path to:

```text
connect device
register one Value
publish/observe it
invoke one Action
```

Do not begin with every library subsystem.

## Concepts

Explain:

- Device
- Cluster
- Namespace reservation
- Resource
- Value
- Action
- Event
- Service
- Bridge
- Backend
- Local MQTT
- Remote MQTT

## Network Architecture

Explain:

- minimum topology
- local cluster topology
- local + remote topology
- bridge role
- backend role
- resilience/local behavior

## Resources

Explain:

- NetValue
- NetAction
- NetEvent
- registration
- local/remote authority
- discovery
- telemetry

## Messaging

Explain:

- Message Dispatcher
- Value state/write
- Action invocation/results
- Event emission
- MQTT mapping
- Console adapter
- request/response

## Runtime & Scheduling

Explain:

- Runtime
- manual loop
- managed task mode
- Scheduler
- Jobs
- wall-clock vs monotonic time
- time synchronization

## Services

Explain reusable higher-level capabilities.

Examples:

- LightController
- OTA
- monitoring
- other reusable services

Clarify that Services expose Resources.

## Examples

Organize by use case, not only by module.

Examples:

- expose a temperature sensor
- control a light
- consume a remote Resource
- invoke an Action
- emit an Event
- schedule a daily Job
- build a local cluster
- bridge local and remote MQTT
- generic consumer/discovery example

## Reference

API-oriented reference generated/maintained alongside conceptual docs.

## Migration / Legacy

Explain:

- TCP predecessor
- ServerVariable migration
- old controller/UI paths
- Timer → Scheduler/Jobs
- other removed/renamed concepts

## MCP

Explain how agents/tools can query NightMare Network documentation and source.

---


# 13.1 Documentation Opening — Conceptual Overview Before Code

The Docs section should **not** begin with API signatures, headers, or class-by-class reference.

The first technical pages should establish a clear mental model of the current NightMare Network architecture.

A new user should understand the major concepts and how they relate **before** seeing detailed implementation examples.

Recommended opening flow:

```text
Docs
  ↓
Overview
  ↓
Key Concepts
  ↓
How the pieces connect
  ↓
Minimum working example
  ↓
Topic-specific guides
  ↓
API / Reference
```

## Overview page

The initial Docs overview should answer:

- What is a NightMare Network participant/device?
- What is exposed to the network?
- How does another participant discover and interact with it?
- What role does MQTT play?
- What is automatic after registration?
- What is handled by Services versus Resources?
- How do local and remote resources differ?
- How do Scheduler/Jobs fit into the system?
- What does the application developer normally need to implement?

The overview should be architectural, not class-reference-oriented.

A good high-level diagram is:

```text
Application / Service
        │
        │ exposes / updates
        ▼
    Resources
        │
        ▼
Resource Registry
        │
        ▼
Resource Manager
        │
        ├── discovery
        ├── state publication
        ├── remote writes
        ├── Actions
        ├── Events
        └── telemetry
        │
        ▼
Message Dispatcher
        │
        ▼
Transport / MQTT
```

This should be explained in prose before diving into individual headers.

---

# 13.2 Key Concepts Section

Create a prominent **Key Concepts** section near the beginning of the Docs.

Each concept should have a short conceptual page that explains:

1. what it is
2. why it exists
3. what responsibility it owns
4. what it does **not** own
5. how it relates to the other concepts
6. one small example

Recommended concepts:

## Resource

The generic network-visible capability.

Explain that Resources are the common model used to expose device state and behavior.

```text
Resource
├── Value
├── Action
└── Event
```

Do not start with inheritance diagrams or constructors.

Start with:

> A Resource is something another NightMare Network participant can observe or interact with.

Then explain the three kinds.

## NetValue

Explain conceptually:

> A `NetValue<T>` represents typed state.

Examples:

```text
temperature
doorOpen
brightness
firmwareVersion
```

Important concepts to explain before code:

- read-only vs read/write
- local authoritative Value vs remote mirrored Value
- last-known state
- freshness where implemented
- state changes versus write requests
- Values do not know MQTT topics

Then show a tiny example.

## NetAction

Explain:

> An Action represents a request to make something happen.

Cover:

- no arguments
- scalar arguments
- structured arguments
- fire-and-forget
- acknowledgement
- result

Explain why Actions are different from writable Values.

Example distinction:

```text
brightness = 50
    → Value write

restart()
    → Action
```

## NetEvent

Explain:

> An Event represents something that happened, not persistent state.

Examples:

```text
buttonPressed
motionDetected
alarmTriggered
```

Explain why:

```text
doorOpened → Event
doorOpen   → Value
```

are different and may coexist.

## Resource Registry

This should be one of the most important conceptual pages.

Explain:

> The Resource Registry is the inventory/schema of the capabilities this participant knows about.

For locally owned Resources, it defines the participant's network-visible interface.

For remote Resources, it allows the runtime to track and resolve mirrors/bindings.

Explain that registering a Resource is the point at which it becomes known to the network machinery.

Use the phrase:

> **Register once, participate automatically.**

Then show what registration enables:

```text
Resource registration
    ↓
discovery
message routing
publication
remote access
telemetry
reconnection behavior
```

Avoid making the Registry sound like a simple C++ container.

Its architectural role is more important than its data structure.

## Resource Manager

Clearly distinguish Registry from Manager.

```text
Registry = what Resources exist
Manager  = what the runtime does with them
```

Explain that the Manager handles:

- authoritative local updates
- remote state application
- remote write requests
- Action dispatch
- Event dispatch
- publication policies
- freshness/runtime state
- interaction with messaging/transport

This page should make clear that callbacks/handlers/runtime behavior live here rather than inside `NetValue`.

## Local vs Remote Resources

Give this its own conceptual explanation.

```text
LOCAL
    this participant is authoritative

REMOTE
    this participant mirrors/consumes another authority
```

Explain why this matters for:

- publishing
- preventing echo loops
- writes
- freshness
- ownership of state

A simple flow diagram should be included.

## Service

Explain:

> A Service implements behavior; Resources are what the network sees.

Example:

```text
LightController
    ↓ exposes
power        Value
brightness   Value
toggle       Action
```

This is a key concept for preventing users from confusing internal implementation classes with protocol-level objects.

## Message Dispatcher / MessageHandler

Explain that this is the common semantic ingress layer.

```text
MQTT ─────┐
Console ──┼──> Dispatcher → Resource Manager
Other ────┘
```

Explain that transports differ, but Value/Action/Event semantics remain the same.

## Scheduler / Job / Runtime

Explain the three separately:

```text
Scheduler = when
Job       = what scheduled work exists
Runtime   = how components are driven/executed
```

Make the distinction from FreeRTOS Tasks explicit.

## Cluster / Local MQTT / Remote MQTT / Bridge

Explain topology concepts before MQTT implementation details.

Especially preserve:

> Local MQTT is a cluster-local coordination bus. The backend does not connect directly to it.

## Namespace

Explain only as far as the implementation currently supports.

If still reserved/planned, label it clearly.

---

# 13.3 Concept Pages Should Precede Class Pages

The docs hierarchy should distinguish between:

```text
Concept
    "What is a NetValue and why does it exist?"

Guide
    "How do I expose a writable brightness Value?"

Reference
    "NetValue<T> API"
```

These must not be collapsed into one page.

A user should be able to understand NightMare Network without reading the API reference.

Recommended pattern:

```text
Resources
├── Overview
├── Values
│   ├── Concept
│   ├── Using local Values
│   ├── Consuming remote Values
│   └── NetValue API
├── Actions
│   ├── Concept
│   ├── Calling Actions
│   └── NetAction API
├── Events
│   ├── Concept
│   ├── Emitting/consuming Events
│   └── NetEvent API
├── Registry
│   ├── Concept
│   ├── Registration
│   └── API
└── Resource Manager
    ├── Concept
    ├── State/write flows
    └── API
```

---

# 13.4 "How the Pieces Work Together" Page

After individual Key Concepts, include one page that connects them.

Example flow for an owned sensor:

```text
Sensor / Service
      ↓
ResourceManager.set()
      ↓
NetValue
      ↓
Registry knows the Resource
      ↓
Manager publishes VALUE_STATE
      ↓
Dispatcher / MQTT
      ↓
remote participant
```

Example flow for a remote writable Value:

```text
Remote consumer
      ↓
VALUE_WRITE
      ↓
Message Dispatcher
      ↓
Resource Manager
      ↓
local write handler / Service
      ↓
actual hardware state changes
      ↓
ResourceManager.set()
      ↓
authoritative VALUE_STATE
```

Example Action flow:

```text
caller
  ↓
ACTION_INVOKE
  ↓
Dispatcher
  ↓
Resource Manager
  ↓
Action handler
  ↓
optional ACK / RESULT
```

This page is critical because individual class pages alone will not communicate the architecture.

---

# 13.5 First Code Only After the Mental Model

After the conceptual overview, provide a minimal end-to-end example.

For example:

```cpp
NetValue<float> temperature("temperature");

void setup() {
    network.resources().add(temperature);
}

void loop() {
    network.resources().set(temperature, readTemperature());
    network.loop();
}
```

Then explain what happened automatically:

```text
1. Resource created
2. Resource registered
3. Registry made it part of the participant schema
4. Manager can publish state
5. Other participants can discover/consume it
```

The first example should demonstrate the architecture rather than introduce every configuration option.

---

# 13.6 MCP Should Follow the Same Concept-First Structure

Because MCP exposes the technical Docs layer, conceptual pages should be first-class MCP content.

Queries such as:

```text
What is ResourceManager?
What is the Registry?
Difference between Action and writable Value?
How does a remote NetValue work?
What happens when I register a Resource?
```

should resolve primarily to conceptual documentation before raw source/API pages.

MCP search/index metadata should distinguish:

```text
concept
guide
reference
example
legacy
```

where practical.

A conceptual question should not require an agent to infer architecture from C++ headers.

---



# 13.7 Responsibility Boundary — What NightMare Does vs What You Implement

The documentation should make the responsibility boundary explicit throughout the site.

A user should not have to infer whether NightMare Network is:

- a complete application framework
- a hardware abstraction layer
- a network/runtime framework
- an automation engine

The docs should repeatedly clarify what the library provides and what remains the responsibility of the device/application programmer.

Use a consistent documentation pattern on major conceptual and practical pages:

```text
What NightMare handles
What your device/application code handles
What NightMare intentionally does not decide
```

This pattern should appear both in the conceptual onboarding layer and in code-oriented guides.

---

## Conceptual responsibility summary

Near the beginning of the Docs, include a high-level section such as:

### NightMare Network handles

- network participation
- Resource registration and discovery
- Resource addressing/routing
- Value / Action / Event semantics
- local and remote Resource roles
- state publication
- remote write routing
- Action invocation/result plumbing
- Event dispatch
- telemetry/publication policies
- common message dispatch
- MQTT integration
- request/response mechanics
- Scheduler / Jobs infrastructure
- Runtime execution integration
- reconnection/network lifecycle behavior
- common reusable Services where the library provides them

### The device programmer handles

- actual device behavior
- hardware drivers and hardware-specific logic
- reading real sensors
- applying writable Resource changes to hardware
- Action handlers and application-specific behavior
- deciding when Events should be emitted
- device-specific Services
- board configuration
- device-specific policy and semantics
- which Resources a device exposes

### NightMare intentionally does not decide

- application/domain meaning
- room/person/scene semantics
- high-level cross-device automation rules
- how a particular actuator physically implements a command
- business logic
- deployment-specific UI behavior
- centralized application orchestration

This reinforces that NightMare provides common infrastructure and primitives while the application defines device meaning.

---

## Per-concept responsibility boxes

Major concept/guide pages should include concrete responsibility boundaries.

### NetValue

```text
NightMare handles:
- registration
- discovery/schema
- publication
- remote mirror updates
- write request routing
- freshness/runtime tracking where supported

You handle:
- reading the real source
- calling the Resource Manager with authoritative state
- deciding what a requested write means
- applying writable state to hardware/application logic

NightMare does not:
- infer the physical meaning of the Value
```

### NetAction

```text
NightMare handles:
- ACTION_INVOKE routing
- typed argument transport
- request correlation
- ACK/RESULT plumbing where configured

You handle:
- the Action handler
- the actual operation
- validation/application-specific acceptance
- the returned result or failure semantics
```

### NetEvent

```text
NightMare handles:
- Event registration
- dispatch
- transport to interested participants

You handle:
- deciding when the Event occurred
- constructing its payload
```

### Resource Registry / Manager

```text
NightMare handles:
- storing/resolving registered Resources
- connecting registered Resources to the network/runtime model
- routing state, writes, Actions and Events

You handle:
- deciding which Resources exist
- registering application/device Resources
- providing the actual behavior behind them
```

### MQTT

```text
NightMare handles:
- topic/message mapping
- subscriptions
- common message routing
- Resource integration
- reconnect behavior

You normally do not:
- manually subscribe for every Resource
- manually decode every Resource command
- manually publish every registered Resource state
```

### Scheduler / Jobs

```text
NightMare handles:
- determining when Jobs are due
- scheduling execution
- Runtime integration

You handle:
- defining what the Job actually performs
```

### Services

```text
NightMare may handle:
- reusable Service infrastructure
- exposing common Service Resources
- lifecycle integration where implemented

You handle:
- application-specific Service behavior
- hardware-specific implementation
- device-specific policy
```

---

## Story/positioning layer

The public Story/Why section should also communicate this boundary in simpler language.

A useful framing is:

> NightMare Network standardizes the common infrastructure around connected devices — communication, Resources, message handling, scheduling, and reusable Services — while leaving the actual device behavior and application meaning to the developer.

This helps users understand the project's scope before entering the technical Docs.

---

## MCP responsibility

Because MCP exposes the technical documentation layer, responsibility information should be indexed as first-class technical content.

Questions such as:

```text
What does NightMare do automatically?
What do I still need to implement?
Do I need to manually publish this Resource?
Who handles Action callbacks?
Does NightMare control the hardware for me?
```

should resolve to the relevant conceptual/guide material.

Where practical, concept metadata should make these sections easy to retrieve.

---


# 14. Homepage Message

The homepage should quickly communicate that NightMare Network is:

> A distributed device/network framework for ESP-family and compatible systems that standardizes communication, resource exposure, message handling, scheduling, and reusable device services across local and remote MQTT networks.

Avoid presenting it as:

- a home automation platform
- a dashboard framework
- only an MQTT wrapper
- only an ESP sensor library
- a backend-centric system

The homepage should show both minimal and expected topologies visually.

---

# 15. "What Can I Use This For?" Section

Add a practical capability-oriented section.

Examples:

- expose sensor data
- control device state remotely
- invoke typed Actions
- emit Events
- discover device capabilities dynamically
- schedule local behavior
- coordinate devices through Local MQTT
- connect clusters to a remote backend
- build generic dashboards/clients
- bridge local and remote networks
- standardize common embedded networking patterns

The goal is to help someone recognize whether NightMare Network fits their use case before reading API reference pages.

---

# 16. Minimum Example Philosophy

Each major concept should have a minimal example.

Examples should answer one question at a time.

### Value

```cpp
NetValue<float> temperature("temperature");
network.resources().add(temperature);
```

### Writable Value

```cpp
NetValue<uint8_t> brightness(
    "brightness",
    NetAccess::READ_WRITE
);
```

### Action

```cpp
NetAction<void> restart("restart");
```

### Event

```cpp
NetEvent<void> buttonPressed("buttonPressed");
```

### Job

```cpp
scheduler.dailyAt(...);
```

Do not require a full production deployment just to explain one concept.

---

# 17. Historical Evolution Page

Create a dedicated page for the evolution of NightMare Network.

Suggested structure:

```text
1. Point-to-point TCP
2. Common message handling
3. MQTT
4. Request/response over MQTT
5. Shared state and ServerVariables
6. Software timers / setTimeout-style utilities
7. Scheduler
8. Services, Controllers, Sensors, Info
9. Net Resources
10. Automatic Resource Registry
11. Current architecture
```

The goal is not nostalgia.

The goal is to explain:

> The current architecture exists because the same device/network problems kept recurring and were progressively standardized.

Mark old systems clearly as historical/legacy when appropriate.

---

# 18. TCP Documentation

TCP should be documented under Legacy/History.

Suggested wording:

> NightMare Network began with a custom TCP server/client mechanism used to trigger actions directly between devices. This model was the predecessor of the current NM-NW architecture. As deployments grew, MQTT-based messaging and generalized message handling replaced direct point-to-point coupling.

Do not present TCP as the recommended current architecture.

---

# 19. ServerVariable Documentation

Explain ServerVariables as an important intermediate step in the evolution toward Resources.

Suggested narrative:

> ServerVariables introduced reusable network-visible state, but later resource requirements expanded beyond variable synchronization to include typed state, writable state, Actions, Events, discovery, and automatic registration. The modern Resource model consolidates these patterns.

Provide migration examples where useful.

---

# 20. Software Timer / Scheduler Documentation

The docs should explain that NightMare accumulated general embedded utilities because they repeatedly appeared in projects.

Examples:

```text
Software Timer
setTimeout-like behavior
Scheduler
Jobs
```

The final docs should not keep historical overlap if the code has been consolidated.

Present the final model:

```text
Scheduler = time-based execution engine
Job       = logical scheduled work
Runtime   = execution policy
```

Use the old Timer/setTimeout story only in the historical/evolution section.

---

# 21. Time Documentation

Document the new Time abstraction independently from synchronization.

Explain:

```text
now()
second()
minute()
hour()
day()
month()
year()
```

as NightMare's stable time API.

Explain that current clock synchronization still uses:

```text
Control/Request → Control/Time
```

and that NTP may be added later.

Clarify:

```text
monotonic time → delays/intervals/timeouts
wall-clock time → calendar Jobs/timestamps
```

Do not document future NTP behavior as already implemented.

---

# 22. Avoid Becoming Home Assistant

The docs should reinforce the intended boundary.

NightMare Network is:

- a distributed device/network framework
- a communication/resource model
- runtime/scheduling infrastructure
- reusable embedded/network services

It is **not** intended to become:

- a home automation platform
- a centralized rules engine
- a room/person/scene ontology
- a historical telemetry database
- a dashboard product
- a global automation coordinator

A higher-level application may use NightMare Network to implement those things.

Suggested principle:

> NightMare Network describes device capabilities and communication. Application-level semantics and complex cross-device automation belong to software built on top of the network.

---

# 23. MCP Goals

The MCP endpoint should become a first-class way for agents and tools to understand the **technical NightMare Network documentation and source**.

It should reflect the same conceptual architecture as the **Docs section of the website**, not the entire public storytelling/positioning layer.

The MCP should help answer questions such as:

- What is a Resource?
- How do I expose a writable Value?
- How do I invoke an Action?
- How does Local MQTT differ from Remote MQTT?
- How do I schedule a Job?
- How do I migrate from ServerVariable?
- Where is `MessageHandler` implemented?
- What service exposes OTA?
- What is the current resource registration API?
- What examples demonstrate remote resource consumption?

The MCP should not require an agent to reconstruct architecture only from source code.

---

# 24. MCP Content Model

The MCP should expose at least:

```text
Conceptual documentation
API/reference documentation
Examples
Source lookup
Architecture/migration docs
Version information
```

Existing tool categories such as:

```text
search_docs
get_doc
list_docs
search_source
get_source
get_api
get_example
get_version
```

fit this direction well.

The content behind those tools should be reorganized to match the new architecture.

---

# 25. MCP Search Expectations

Search should surface conceptual pages before obscure implementation details when the query is architectural.

Examples:

Query:

```text
resource
```

should return:

- Resource model overview
- NetValue
- NetAction
- NetEvent
- ResourceRegistry/Manager

before legacy ServerVariable documentation.

Query:

```text
scheduler
```

should prioritize:

- Scheduler
- Jobs
- Runtime execution model
- Time abstraction

before historical Software Timer material.

Query:

```text
mqtt
```

should surface:

- Local vs Remote MQTT
- Transport architecture
- Message mapping
- request/response
- bridge topology

not only the MQTT client class reference.

---

# 26. MCP Source Context

Source retrieval should preserve architectural context.

If an agent asks for:

```text
LightController
```

the MCP should ideally make it easy to also discover:

```text
Service concept
Resources exposed by LightController
Resource registration
related examples
```

Likewise:

```text
MessageHandler
```

should be contextualized as Network/Dispatcher infrastructure, not an `xtra` utility.

This does not necessarily require changing the MCP protocol itself.

It may be achieved through:

- better document organization
- richer indexed metadata
- cross-links
- consistent names
- source/document associations

---

# 27. MCP and Legacy Content

Legacy content must remain searchable but should be clearly marked.

Examples:

```text
Legacy/TCP
Legacy/ServerVariable migration
```

Search results should distinguish:

```text
Current
Legacy
Historical
Reserved/Future
```

where practical.

An agent should not accidentally recommend the TCP module simply because its documentation contains more old examples.

---

# 28. MCP and Version Awareness

The MCP should expose current library version and, where practical, document/API version compatibility.

Agents should be able to distinguish:

- current API
- removed API
- Legacy modules
- planned/reserved concepts

Avoid mixing future namespace/NTP behavior into responses as if already implemented.

---

# 29. Docs and MCP Must Share One Source of Truth

Avoid maintaining one conceptual explanation for the website and another for MCP.

Where possible:

```text
Markdown docs
    ↓
Website rendering
    ↓
MCP indexing/search
```

should use the same underlying content.

This minimizes divergence between:

- what humans read
- what agents retrieve
- what the source actually implements

Generated API reference may remain separate, but conceptual docs should be shared.

---

# 30. Documentation Metadata

Add lightweight metadata/frontmatter where useful to improve website navigation and MCP search.

Possible fields:

```text
title
section
status
aliases
related
legacy
since
```

Example:

```yaml
title: NetValue
section: Resources
status: current
aliases:
  - network value
  - sensor value
related:
  - Resource Registry
  - NetAction
  - NetEvent
```

Do not over-engineer this into a large documentation database.

The goal is better navigation/search.

---

# 31. Terminology Consistency

The website and MCP must use the same canonical terms:

```text
NightMare Network
NM-NW
Device
Cluster
Namespace
Resource
Value
Action
Event
Service
Resource Registry
Resource Manager
Message Dispatcher
Scheduler
Job
Runtime
Local MQTT
Remote MQTT
Bridge
Backend
Legacy
```

Avoid using multiple historical names for the same current concept unless explaining migration/history.

---

# 32. Documentation Status Labels

Use clear labels where necessary:

```text
Current
Legacy
Historical
Experimental
Reserved / Planned
```

Examples:

```text
TCP                 → Legacy
ServerVariable      → Legacy / Migration
Namespaces          → Reserved / Planned
NTP                 → Planned
Net Resources       → Current
Resource Registry   → Current
```

Do not make users infer status from context.

---

# 33. Example Organization

Examples should be discoverable by capability.

Suggested categories:

```text
Basics
    minimal device
    Value
    Action
    Event

Resources
    read-only sensor
    writable state
    remote mirror
    continuous telemetry

Messaging
    request/response
    Action with arguments
    Action with result
    Console adapter

Scheduling
    periodic Job
    daily Job
    local automation

Topology
    two-device Local MQTT
    cluster
    bridge to Remote MQTT
    backend consumer

Services
    LightController
    OTA
    monitoring
```

Do not organize examples only by historical folder structure.

---

# 34. Architecture Diagrams

Use diagrams liberally in conceptual docs.

At minimum include:

1. original TCP point-to-point model
2. minimum MQTT topology
3. local cluster topology
4. local + remote + backend topology
5. Resource/Service relationship
6. message dispatch flow
7. Resource Registry auto-integration
8. Scheduler/Job/Runtime relationship
9. evolution timeline

Diagrams should explain system boundaries, not merely decorate the page.

---

# 35. Recommended Website Flow for a New User

A new user should be able to follow:

```text
What is NightMare Network?
    ↓
How devices communicate
    ↓
Expose one Value
    ↓
Control one writable Value
    ↓
Invoke one Action
    ↓
Emit one Event
    ↓
Understand Resource Registry
    ↓
Understand Scheduler/Jobs
    ↓
Build a cluster
    ↓
Bridge to remote/backend
```

This progression is preferable to forcing users to learn every internal module before doing anything useful.

---

# 36. Migration Guides

Create targeted migration pages where old APIs were common.

At minimum consider:

```text
TCP → NM-NW/MQTT
ServerVariable → NetValue
Sensor/Info registration → Resource Registry
Controller external API → Resources
Timer → Scheduler/Job
direct MessageHandler command → NetAction
TimeLib direct use → NMTime adapter
```

Migration pages should explain *why* the replacement exists, not only syntax changes.

---

# 37. API Reference Philosophy

Reference pages should answer:

- what the class/type represents
- when to use it
- ownership/lifetime expectations
- memory/runtime implications where relevant
- important interactions with Resource Manager/Runtime
- minimal example
- related concepts

Avoid reference pages that only reproduce function signatures.

---

# 38. Embedded Constraints Should Be Visible

NightMare targets many ESP-family devices and may coexist with graphics/UI workloads.

The docs should be transparent about design choices intended to keep memory predictable.

Examples:

- Resources avoid unnecessary strings
- callbacks live in Manager implementations
- one task per subsystem is not the default
- Resources do not own MQTT clients
- telemetry timers are centralized
- metadata is optional
- templates are kept small where practical

This is useful architectural context for advanced users.

---

# 39. Website Voice

Preferred voice:

- explanatory
- direct
- capability-oriented
- implementation-aware
- neutral about deployment choices

Avoid:

```text
"We always..."
"Our dashboard..."
"Our backend requires..."
```

unless discussing historical context.

Prefer:

```text
"You can..."
"A device may..."
"A deployment can..."
"NightMare Network supports..."
```

Do not over-market.

Technical clarity is more important than promotional language.

---

# 40. Post-Rewrite Documentation Deliverables

After the code rewrite, the documentation/MCP agent should produce at least:

## Conceptual
- What is NightMare Network?
- Architecture overview
- Evolution/history
- Cluster/local/remote topology
- Resource model
- Message model
- Runtime/Scheduler/Jobs
- Time model
- Services
- Legacy overview

## Practical
- Getting Started
- first Value
- writable Value
- Action
- Event
- Resource Registry
- remote resource consumption
- telemetry
- daily Job
- cluster setup
- bridge/backend example

## Migration
- TCP
- ServerVariable
- old Sensors/Info patterns
- controller exposure
- Timer/Scheduler
- MessageHandler direct commands
- TimeLib

## Reference
- current API
- current folders/modules
- include structure
- resource types
- manager/runtime APIs

## MCP
- reorganized indexes
- current vs Legacy status
- cross-linking
- examples
- source associations
- version awareness

---


# 40.1 Branding and Website UI Refresh

The post-refactor website pass should also replace remaining MattediWorks/MW-centric presentation with NightMare Network's own visual identity.

A project/folder named:

```text
newbranding/
```

will contain or define the new NightMare Network branding assets/direction.

The website agent should inspect and use that branding rather than reusing MW branding as the primary identity.

Required website/UI changes include:

## NightMare Network branding

- replace MW-centric logo/branding with the NightMare Network branding from `newbranding/`
- make NightMare Network visually identifiable as its own project
- preserve appropriate MattediWorks attribution where relevant without making MW branding the main project identity

## GitHub link

Use a recognizable **GitHub icon** for repository/source navigation instead of a plain text-only treatment where appropriate.

The icon should be accessible and still expose a readable label/tooltip for clarity.

## Dark / light mode control

Replace the current dark/light mode UI with a better dedicated component.

Before implementing it, inspect the separate `mwsite` project that will be linked/provided on disk.

Use that project as a reference for:

- dark/light mode interaction
- visual component quality
- theme persistence behavior
- system theme handling where applicable
- iconography and accessibility

Do not blindly copy unrelated `mwsite` architecture; use it as a design/behavior reference for the theme control.

## Design consistency

The storytelling layer and Docs layer should share one coherent NightMare Network visual system even though their content goals differ.

The branding/UI pass should not compromise documentation usability.

Prioritize:

1. clear navigation
2. readable technical content
3. strong code/example presentation
4. responsive diagrams/layout
5. accessible dark/light themes
6. distinct NightMare Network branding

# 41. Acceptance Criteria

The website/docs/MCP rewrite is successful when:

### Delivery sequence
- The library refactor is completed first without requiring website/MCP changes during the refactor.
- The website/docs/MCP pass is based on the final refactored APIs and architecture.

### Website separation
- The website clearly provides a Story/Why/Evolution experience and a separate technical Docs experience.
- Story content explains motivation, evolution, and capabilities without replacing technical documentation.
- Docs remain task-oriented and implementation-accurate.
- MCP indexes the Docs/source/reference surface rather than acting as a mirror of the storytelling layer.

### Narrative
- A new user can understand what NightMare Network is before seeing class names.
- The TCP origin and MQTT evolution are explained clearly.
- The history explains the present architecture rather than dominating the docs.
- The framework is presented as usable by others, not only as an internal deployment.

### Topology
- Local MQTT is clearly cluster-local.
- Backend never appears as a Local MQTT client.
- Bridge role is clear.
- Distributed and backend-assisted usage are both represented.
- Namespace is described only to the extent actually implemented/planned.

### Conceptual onboarding
- The Docs begin with an architectural overview before API/class reference.
- Key Concepts include Resource, NetValue, NetAction, NetEvent, Resource Registry, Resource Manager, local/remote Resources, Service, Dispatcher, Scheduler/Job/Runtime, and topology.
- Registry and Manager are explained conceptually, not merely as C++ containers/classes.
- A dedicated "How the pieces work together" page shows end-to-end message/resource flows.
- Concept pages, usage guides, and API reference are separate layers.
- MCP can retrieve these conceptual pages directly.
- The Docs clearly explain what NightMare handles, what device/application code must implement, and what NightMare intentionally does not decide.
- Major concept/guide pages repeat this responsibility boundary in context.

### Resources
- Value/Action/Event are central concepts.
- Resource Registry auto-integration is documented.
- Generic discovery/consumption is clear.
- Services are shown as implementations exposing Resources.

### Messaging
- Message handling is described as common across ingress sources.
- MQTT request/response is documented.
- Console is shown as an adapter, not a parallel protocol.
- Action arguments/results are explained.

### Scheduling
- Scheduler, Job, and Runtime are clearly separated.
- Local scheduled behavior is documented without turning NightMare into an automation platform.
- Time abstraction and synchronization are explained accurately.

### Legacy
- TCP is visibly Legacy/historical.
- ServerVariable is explained as an intermediate architecture and migration path.
- Old concepts do not outrank current APIs in navigation/search.

### MCP
- MCP search returns current architecture first.
- Agents can retrieve technical conceptual docs, examples, source, and API reference.
- MCP is aligned with the Docs layer, not required to expose the Story/marketing layer.
- Current/Legacy/Planned distinctions are visible.
- Website Docs and MCP share the same conceptual source where practical.

### Branding/UI
- NightMare Network uses its own branding from `newbranding/` rather than MW branding as the primary project identity.
- Repository navigation uses a GitHub icon where appropriate.
- The dark/light mode control is replaced with a better dedicated component.
- The `mwsite` project provided on disk is inspected as a reference for theme-switching behavior/design.
- Story and Docs sections remain visually coherent under the new branding.

### Tone
- Docs consistently describe what NightMare Network **can do** and **how a user can use it**.
- Deployment-specific language such as "The Dashboard" is removed from core architecture.
- The docs do not imply NightMare is Home Assistant or a centralized automation platform.

---

# 42. Architectural Statements the Public Docs Should Reinforce

> **NightMare Network began as direct device-to-device TCP communication and evolved into a reusable distributed device/network framework.**

> **MQTT removed application-level point-to-point coupling while still using broker infrastructure.**

> **NightMare Network supports both distributed local behavior and backend-assisted deployments.**

> **Local MQTT is a cluster-local coordination bus; the backend does not connect to it.**

> **Common message handling allows multiple ingress sources to converge on one semantic model.**

> **Values are state, Actions are requests, Events are occurrences.**

> **The Resource Registry makes a device's capabilities discoverable and manageable without repeated manual wiring.**

> **Services implement behavior; Resources expose that behavior to the network.**

> **Scheduler and Jobs provide local time-based behavior without becoming a global automation engine.**

> **NightMare Network provides infrastructure and primitives; higher-level applications decide what those primitives mean in their domain.**

> **The documentation should teach what NightMare Network is capable of and how others can build with it.**

---

# 43. Agent Guidance

When rewriting the website and MCP:

1. Read the final rewritten library architecture and public APIs first.
2. Do not document transitional code as final architecture.
3. Preserve useful historical context, but keep current APIs dominant.
4. Prefer capability/use-case pages over class-first navigation.
5. Keep diagrams and examples aligned with actual implementation.
6. Ensure MQTT topic examples match the final transport mapping.
7. Ensure MCP indexes current conceptual docs before Legacy material.
8. Do not claim namespace, NTP, timezone, or other reserved features are implemented unless they are.
9. Keep website terminology identical to code terminology.
10. Treat the Resource Registry and Resource model as the main bridge between embedded code, backend consumers, UI consumers, and agent tooling.
11. Keep the Website Story layer and the technical Docs layer intentionally distinct.
12. Keep MCP scoped primarily to Docs/source/reference.
13. Inspect `newbranding/` before redesigning the website identity.
14. Inspect the `mwsite` project provided on disk before implementing the new dark/light mode control.
15. On major Docs pages, explicitly distinguish what NightMare handles from what the device/application programmer must implement.
16. Preserve the framework boundary: infrastructure and primitives belong to NightMare; device semantics and application/domain logic belong to the user/application.

