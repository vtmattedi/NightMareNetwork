---
title: Resources
description: Declare, bind, publish, observe, write, and invoke NightMare Resources.
section: modules
order: 10
---

# Resources

Resources are NightMare's application-facing network abstraction.

A Resource says:

```text
what capability exists
who owns it
whether it is state or an operation
how another device may interact with it
```

The Resource layer deliberately separates application types from MQTT transport.

```text
application                     transport
    T
    │
NetValue<T>
    │  NetCodec<T>
    ▼
encoded String  ───────────────> MQTT
```

`ResourcesManager` never needs to know the application's `T`.

## Class model

```text
NetResource
├── NetValueResource
│   └── NetValue<T>
│       ├── ManagedSensor<T>
│       ├── RemoteSensor<T>
│       ├── ManagedState<T>
│       └── RemoteState<T>
└── NetActionResource
    ├── ManagedAction
    └── RemoteAction
```

The normal application API is the six leaf types.

`NetValue<T>` remains public for cases that do not fit the standard wrappers, but most projects should prefer the semantic wrappers because they make ownership, access, and synchronization intent obvious.

## Managed vs Remote

A Resource's role is fixed when it is constructed.

```text
Managed
    this device implements the Resource

Remote
    another device implements the Resource
```

The role does not change.

A Remote Resource can be retargeted to another source, but it remains Remote.

## Binding

Resources are owned by the application, not by `ResourcesManager`.

A Resource must therefore outlive its binding.

```cpp
ManagedSensor<float> temperature("temperature");

void setup()
{
    gResourcesManager.bindResource(&temperature);
    startNightMareESP();
}
```

The manager stores a non-owning pointer.

A project can later remove the binding:

```cpp
gResourcesManager.unbindResource(&temperature);
```

## Why bind before `startNightMareESP()`

The normal order is:

```cpp
bind Resources
register handlers
startNightMareESP()
```

This has two benefits.

First, Resources are already registered when MQTT starts, so normal manifest/state announcement can happen immediately.

Second, if an adopted identity still has retained state to clean up, the cleanup pass can only discover Managed Resources declared by the current firmware.

## ManagedSensor

A `ManagedSensor<T>` is:

```text
local owner
READ
STRICT
```

Use it for state produced by this device that other devices may observe but not write.

```cpp
ManagedSensor<float> roomTemperature("temperature");

void updateTemperature(float value)
{
    roomTemperature.setValue(value);
}
```

`setValue()` updates authoritative local truth.

If the Resource is bound and MQTT transport is available, NightMare publishes the state retained at:

```text
<device>/resources/temperature/state
```

If transport is unavailable, the local value still changes. Reconnect re-announcement publishes the authoritative state later.

## RemoteSensor

A `RemoteSensor<T>` observes a read-only Value owned by another device.

```cpp
RemoteSensor<float> outdoorTemperature(
    "temperature",
    NetDeviceIdentity("weather-node"));
```

RemoteSensor uses:

```text
role:        REMOTE
access:      READ
sync:        STRICT
```

`getValue()` therefore reflects the owner's authoritative state directly.

### Receiving updates

Install `onUpdate` when application code needs to react to an effective Value change:

```cpp
void temperatureChanged(NetValue<float> &resource, const float &value)
{
    Serial.println(value);
}

RemoteSensor<float> outdoorTemperature(
    "temperature",
    NetDeviceIdentity("weather-node"));

void setup()
{
    outdoorTemperature.onUpdate = temperatureChanged;
    gResourcesManager.bindResource(&outdoorTemperature);
}
```

`onUpdate` is not a packet-received callback.

It fires only when the **effective value** returned by `getValue()` changes.

Repeated owner packets with the same Value do not fire it.

## ManagedState

A `ManagedState<T>` is a writable Value owned by this device.

```cpp
ManagedState<bool> power("power");
```

It publishes retained state exactly like a ManagedSensor, but also subscribes to:

```text
<device>/resources/power/set
```

Remote requests are decoded before application code sees them.

The application decides whether to accept the request:

```cpp
bool onPowerWrite(ManagedState<bool> &state, const bool &requested)
{
    if (!setHardwarePower(requested))
        return false;

    return true;
}

ManagedState<bool> power("power");

void setup()
{
    power.onWrite = onPowerWrite;
    gResourcesManager.bindResource(&power);
}
```

Returning `true` means the requested Value becomes authoritative state.

Returning `false` leaves state unchanged.

The application handler should therefore return `true` only when the request is accepted as the new device truth.

## Local writes to ManagedState

The owner can also change the state directly:

```cpp
power.setValue(true);
```

This does not call `onWrite`.

`onWrite` is the ingress policy for remote `/set` requests. `setValue()` expresses local owner intent directly.

## RemoteState

A `RemoteState<T>` represents a writable Value implemented by another device.

```cpp
RemoteState<bool> bedroomPower(
    "power",
    NetDeviceIdentity("bedroom-ac"));
```

Calling:

```cpp
bedroomPower.setValue(true);
```

publishes:

```text
bedroom-ac/resources/power/set
```

The call returns `true` only if the request was accepted for transport.

It does not mean the remote device accepted the requested state.

## Optimistic RemoteState behavior

RemoteState uses `OPTIMISTIC` synchronization by default.

After a successful local write:

```cpp
bedroomPower.setValue(true);
```

the locally visible `getValue()` becomes `true` immediately for the optimistic window.

The owner's last reported state remains available separately:

```cpp
bedroomPower.authoritativeValue();
```

The default optimistic window is:

```text
5000 ms
```

and can be changed per Resource:

```cpp
bedroomPower.optimisticWindowMs = 2000;
```

Owner state remains authoritative.

The optimistic window only prevents a delayed old owner packet from making the application briefly flicker back immediately after a local request.

## Effective vs authoritative Value

For any `NetValue<T>`:

```cpp
resource.getValue();
```

returns the effective application-facing Value.

```cpp
resource.authoritativeValue();
```

returns the owner's last reported Value.

For Managed Values and STRICT Remote Values those are normally the same.

For an optimistic RemoteState they can temporarily differ.

## Freshness

Remote Value freshness is:

```cpp
ResourceFreshness::UNKNOWN
ResourceFreshness::FRESH
ResourceFreshness::STALE
```

Useful helpers/state include:

```cpp
resource.freshness
resource.isStale()
resource.hasAuthoritativeValue()
resource.lastUpdateMs()
resource.lastWriteMs()
```

State begins `UNKNOWN`.

A valid owner `/state` update makes it `FRESH`.

An empty owner `/state` tombstone makes it `STALE`.

The last known decoded Value stays readable when state becomes STALE.

Retargeting the Resource clears state learned from the old source completely.

## Deferred-source Remote Resources

RemoteSensor, RemoteState, and RemoteAction can be created before their source is known.

Example:

```cpp
RemoteSensor<float> selectedTemperature;

void setup()
{
    gResourcesManager.bindResource(&selectedTemperature);
}
```

Later:

```cpp
selectedTemperature.setSource("weather-node", "temperature");
```

A source-less Remote Resource is registered but has no network address or ingress subscription.

This is useful when the source comes from configuration or later discovery.

## Retargeting

Remote Value wrappers expose:

```cpp
setSource(deviceName, resourceName);
```

Retargeting:

- unsubscribes the previous source,
- clears Value state learned from the old source,
- updates manifest subscriptions,
- subscribes to the new source if valid.

The Resource remains Remote.

If the requested source is invalid, points at the current device, or duplicates another locally bound Resource address, the manager refuses it and leaves the Resource detached.

## ManagedAction

Use `ManagedAction` when this device exposes an operation rather than persistent state.

Without argument metadata:

```cpp
ManagedAction rebootPeripheral("reboot_peripheral");
```

With a schema:

```cpp
static const ActionArgMetadata TimerArgs[] = {
    {"seconds", NetValueType::INTEGER, true},
    {"mode", NetValueType::STRING, false},
};

ManagedAction setTimer("set_timer", TimerArgs);
```

The schema must outlive the Action, so static/global arrays are the normal source.

Install a handler:

```cpp
ActionResult onSetTimer(ManagedAction &action, const String &payload)
{
    // Parse the canonical payload as required by the application.
    return {true, "timer accepted"};
}

void setup()
{
    setTimer.onInvoke = onSetTimer;
    gResourcesManager.bindResource(&setTimer);
}
```

The Action is described in the manifest and subscribes to:

```text
<device>/resources/set_timer/invoke
```

## ActionResult

Managed Action execution returns:

```cpp
struct ActionResult
{
    bool success;
    String result;
};
```

Ordinary raw MQTT `/invoke` does not have a response topic, so the remote sender does not receive this result automatically.

The result is still useful for local/correlated execution paths. `ResourcesManager::executeAction()` preserves it directly, and the `>` Resource-command adapter can surface a ManagedAction result through the normal command/MQTTP response path.

## RemoteAction

A `RemoteAction` invokes an Action owned by another device.

Normal form:

```cpp
RemoteAction setTimer(
    "set_timer",
    NetDeviceIdentity("bedroom-ac"));
```

Invoke it:

```cpp
setTimer.invoke(R"({"seconds":300})");
```

A `true` return means the request was accepted for MQTT publication.

It does not mean the remote handler returned success.

A RemoteAction does not need to duplicate the remote schema.

The normal lightweight declaration carries no local schema at all.

## Optional expected Action schema

A caller may declare the subset of arguments it knows and expects:

```cpp
static const ActionArgMetadata KnownTimerArgs[] = {
    {"seconds", NetValueType::INTEGER, true},
};

RemoteAction setTimer(
    "set_timer",
    NetDeviceIdentity("bedroom-ac"),
    KnownTimerArgs);
```

This is the caller's expectation, not a claim that it mirrors the complete remote contract.

Manifest comparison can warn about disagreements.

## Action payload assertion

By default:

```cpp
NM_ENABLE_ACTION_PAYLOAD_ASSERTION 0
```

Action schema is still published, but generic payload validation is disabled.

When enabled, NightMare validates known Action arguments against a JSON object.

Unknown fields are allowed and optional declared fields may be absent.

This keeps schema checking tolerant rather than turning manifests into a rigid version lock.

## Value codecs

Built-in `NetCodec<T>` support currently covers:

```text
bool
integral types
floating-point types
String
```

Manifest wire types are:

```text
boolean
integer
float
string
struct
```

Unsupported C++ Value types fail to compile unless the project provides an appropriate codec specialization.

## Empty payload rule

Values and Actions deliberately differ.

For a **Value**:

```text
empty payload = retained deletion / unavailable state
```

Therefore an empty String cannot be a valid Resource String value.

For an **Action**:

```text
empty payload = valid invocation payload
```

A zero-argument Action can therefore be invoked with:

```cpp
action.invoke();
```

## Resource command bridge

When Console + Resources are enabled, a command beginning with `>` is routed to `ResourcesManager::executeCommand()` before the generic command parser.

Examples:

```text
> temperature
> identify
> identify action {"mode":"blink"}
> bedroom-ac/resources/power/set true
> bedroom-ac/resources/identify/invoke {"mode":"blink"}
```

A bare unique Value name returns its **effective current value**, so an optimistic RemoteState can return its active optimistic value. A bare Action name invokes an empty-payload Action.

If more than one bound Resource has the same short name, the command is rejected as ambiguous and a full MQTT-shaped topic is required.

For `> name action payload`, everything after the `action` token is one opaque Resource payload.

The full-topic form supports `/invoke`, `/set`, and `/state` subject to Resource kind/ownership. `/state` is accepted only for a Remote Value. A Managed writable Value `/set` uses the same decode + `onWrite` path as MQTT ingress. A Remote `/set` or `/invoke` only reports transport acceptance.

For a ManagedAction, command execution returns its local `ActionResult`.

The Resource payload limit remains 2048 bytes. The leading `>` is a command/control adapter; it does not change the Resource MQTT protocol.

## Manager participation

`ResourcesManager` owns:

```cpp
bindResource(...)
unbindResource(...)
announceAll()
subscribeAll()
setManifestHandler(...)
```

Applications normally call only binding and optional manifest-handler methods.

MQTT transport is injected into the manager by the NightMare MQTT facade.

## Manifest handler

Discovery/tooling code can observe valid manifests:

```cpp
void manifestReceived(const String &deviceName, const String &manifest)
{
    // inspect/update application discovery state
}

gResourcesManager.setManifestHandler(manifestReceived);
```

The callback also receives an empty String for a valid retained manifest withdrawal.

Malformed manifests are not delivered to the handler.

## Limits

Current Resource limits are:

```text
bound Resources:          100
Resource-name length:     64 characters
Value/Action payload:     2048 bytes
manifest JSON capacity:   16384 bytes
```

For wire details, see [Resource protocol](../protocols/resources.md).
