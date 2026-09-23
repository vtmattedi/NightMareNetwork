---
title: Reference
description: Compact reference for public APIs, feature flags, limits, and current compile-time dependencies.
section: modules
order: 90
---

# Reference

This is a compact map of the current public surface.

For behavior and rationale, use the dedicated module/protocol pages. For exact declarations, the source remains authoritative.

## Umbrella headers

```cpp
#include <NightMare.h>
```

Compatibility umbrella:

```cpp
#include <NightMareNetwork.h>
```

The latter currently includes `NightMare.h`.

## DeviceIdentity

Global:

```cpp
extern DeviceIdentity gDeviceIdentity;
```

Main methods:

```cpp
bool begin();

const String &getDeviceName();
const String &getDeviceId();
const String &getHardwareSignature();
const String &getTimezone();

bool isDevice(const String &topic);
bool relativeTopic(const String &topic, String &relative);
String topic(const String &relative);

void lockAddress();

static bool validDeviceName(const String &name);
static bool validTimezone(const String &timezone);

bool setTimezone(const String &timezone);

bool beginAdoption(const String &newName);

bool hasPendingIdentityCleanup();
bool getPendingIdentityCleanup(PendingIdentityCleanup &cleanup);
bool markIdentityCleanupComplete(IdentityCleanupFlags flag);
```

Cleanup flags:

```cpp
CLEANUP_RESOURCES
CLEANUP_STATUS
```

## Resource types

Base model:

```text
NetResource
NetValueResource
NetValue<T>
NetActionResource
```

Normal application wrappers:

```cpp
ManagedSensor<T>
RemoteSensor<T>
ManagedState<T>
RemoteState<T>

ManagedAction
RemoteAction
```

Roles:

```cpp
enum class ResourceRole
{
    MANAGED,
    REMOTE
};
```

Access:

```cpp
enum class AccessPolicy
{
    READ,
    READ_WRITE
};
```

Freshness:

```cpp
enum class ResourceFreshness
{
    UNKNOWN,
    FRESH,
    STALE
};
```

Remote synchronization:

```cpp
enum class NetSyncStrategy
{
    OPTIMISTIC,
    STRICT
};
```

## NetValue<T>

Important methods/state:

```cpp
const T &getValue() const;
const T &authoritativeValue() const;

bool setValue(const T &value);

String encodedValue() const;
String encodedCurrentValue() const;
bool requestEncodedValue(const String &encoded);

ResourceFreshness freshness;
bool isStale() const;
bool hasAuthoritativeValue() const;
bool hasCurrentValue() const;

uint32_t lastUpdateMs() const;
uint32_t lastWriteMs() const;

uint32_t optimisticWindowMs;
UpdateHandler onUpdate;
```

The built-in optimistic window default is:

```text
5000 ms
```

## Remote Value source selection

RemoteSensor and RemoteState expose:

```cpp
void setSource(
    const String &deviceName,
    const String &resourceName);
```

## ManagedState<T>

Handler:

```cpp
using WriteRequestHandler =
    bool (*)(ManagedState<T> &state, const T &requested);
```

Field:

```cpp
WriteRequestHandler onWrite;
```

Returning `true` accepts the requested value as authoritative state.

## NetValueType

Wire taxonomy:

```cpp
enum class NetValueType : uint8_t
{
    STRING,
    BOOLEAN,
    INTEGER,
    FLOAT,
    STRUCT
};
```

Built-in `NetCodec<T>` support:

```text
String
bool
integral types except bool
floating-point types
```

## Action metadata

```cpp
struct ActionArgMetadata
{
    const char *name;
    NetValueType type;
    bool required;
};
```

Constructor:

```cpp
ActionArgMetadata(
    const char *argName,
    NetValueType argType,
    bool argRequired = true);
```

## ActionResult

```cpp
struct ActionResult
{
    bool success;
    String result;
};
```

## ManagedAction

Constructors:

```cpp
ManagedAction(const String &resourceName);

template <size_t N>
ManagedAction(
    const String &resourceName,
    const ActionArgMetadata (&args)[N]);

ManagedAction(
    const String &resourceName,
    const ActionArgMetadata *args,
    size_t argCount);
```

Handler:

```cpp
using Handler =
    ActionResult (*)(ManagedAction &action, const String &payload);

Handler onInvoke;
```

Execution:

```cpp
ActionResult execute(const String &payload) override;
```

## RemoteAction

Common constructors:

```cpp
RemoteAction();

RemoteAction(
    const String &resourceName,
    const NetDeviceIdentity &owner);

template <size_t N>
RemoteAction(
    const String &resourceName,
    const NetDeviceIdentity &owner,
    const ActionArgMetadata (&args)[N]);
```

It also has pointer/count forms for an expected schema.

Methods:

```cpp
void setSource(
    const String &deviceName,
    const String &resourceName);

bool invoke(const String &payload = String());
```

## Resource topic helpers

```cpp
const String &resolveResourceOwner(
    const NetResource &resource);

String resolveResourceManifestTopic(
    const String &deviceName);

String resolveResourceTopic(
    const NetResource &resource,
    ResourceTopicOperation operation);

String resolveResourceTopic(
    const String &deviceName,
    const String &resourceName,
    ResourceTopicOperation operation);
```

Operations:

```cpp
STATE
SET
INVOKE
```

## ResourcesManager

Global:

```cpp
extern ResourcesManager gResourcesManager;
```

Capacity:

```cpp
ResourcesManager::MaxResources == 100
```

Project-facing methods:

```cpp
bool bindResource(NetResource *resource);
void unbindResource(NetResource *resource);

bool announceAll();
void subscribeAll();

bool handleIngressMessage(
    const String &topic,
    const String &message);

using ManifestHandler =
    void (*)(const String &deviceName, const String &manifest);

void setManifestHandler(ManifestHandler handler);

ActionResult executeAction(
    NetActionResource &action,
    const String &canonicalPayload);

ActionResult executeCommand(
    const String &expression);

// Resource command grammar:
//   >list
//   >raw <topic> [payload]
//   > <name> [get|set|invoke] [payload]

bool withdrawIdentity(const String &oldDeviceName);
```

Transport boundary:

```cpp
class ResourcePublisher
{
public:
    virtual bool publish(
        const String &topic,
        const String &payload,
        bool retained) = 0;
};

class ResourceSubscriber
{
public:
    virtual bool subscribe(
        const String &topicFilter) = 0;

    virtual bool unsubscribe(
        const String &topicFilter) = 0;
};
```

## Resource limits

Current limits:

```text
Resources per manager:          100
Resource/topic segment:         64 characters
Value/Action payload:           2048 bytes
Resource manifest capacity:     16384 bytes
Resource command expression:    16640 bytes
```

## Scheduler

Global:

```cpp
extern Scheduler gScheduler;
```

Capacity:

```cpp
Scheduler::MaxJobs == 30
```

Clocks:

```cpp
enum class SchedulerClock
{
    Wall,
    Monotonic
};
```

Run modes:

```cpp
enum class SchedulerRunMode
{
    TASK,
    MANUAL
};
```

Scopes:

```cpp
enum class SchedulerJobScope
{
    MANAGED,
    USER
};
```

Callback type:

```cpp
using SchedulerCallback = void (*)();
```

Startup:

```cpp
bool begin(
    SchedulerRunMode mode = SchedulerRunMode::TASK);

SchedulerRunMode runMode() const;

void tick();
```

Scheduling:

```cpp
int32_t atWall(
    const String &label,
    const String &command,
    uint32_t epochSeconds,
    SchedulerJobScope scope = SchedulerJobScope::MANAGED);

int32_t atWall(
    const String &label,
    SchedulerCallback callback,
    uint32_t epochSeconds);

int32_t after(
    const String &label,
    const String &command,
    uint32_t delayMs,
    SchedulerJobScope scope = SchedulerJobScope::MANAGED);

int32_t after(
    const String &label,
    SchedulerCallback callback,
    uint32_t delayMs);

int32_t everyWall(
    const String &label,
    const String &command,
    uint32_t intervalSeconds,
    SchedulerJobScope scope = SchedulerJobScope::MANAGED);

int32_t everyWall(
    const String &label,
    SchedulerCallback callback,
    uint32_t intervalSeconds);

int32_t everyMonotonic(
    const String &label,
    const String &command,
    uint32_t intervalMs,
    SchedulerJobScope scope = SchedulerJobScope::MANAGED);

int32_t everyMonotonic(
    const String &label,
    SchedulerCallback callback,
    uint32_t intervalMs);

int32_t timer(
    const String &label,
    SchedulerCallback callback,
    uint32_t intervalMs);

int32_t setTimeout(
    SchedulerCallback callback,
    uint32_t delayMs);
```

Management:

```cpp
bool remove(
    const String &label,
    SchedulerJobScope scope = SchedulerJobScope::MANAGED);

bool remove(
    uint32_t id,
    SchedulerJobScope scope = SchedulerJobScope::MANAGED);

bool clear(SchedulerJobScope scope);

String list(SchedulerJobScope scope);
```

Scheduler implementation constants:

```text
max jobs:               30
max label length:       64
max command length:     NM_MAX_MESSAGE_LEN
max interval:           0x7fffffff
task poll:              100 ms
default stack:          8192 bytes
default priority:       1
storage retry:          5000 ms
persistent file:        /jobs.json
```

## Command types

Sources:

```cpp
enum CommandSource
{
    NM_CMD_SRC_UNKNOWN,
    NM_CMD_SRC_MQTT,
    NM_CMD_SRC_SERIAL,
    NM_CMD_SRC_HTTP,
    NM_CMD_SRC_WEBSOCKET,
    NM_CMD_SRC_JOB,
    NM_CMD_ANS_DO_NOT_RESPOND = 0xFF
};
```

Context:

```cpp
struct NightmareContext
{
    CommandSource msgSource;
    String sourceIdentifier;
    void *userContext;
};
```

Result:

```cpp
struct NightMareResults
{
    bool result;
    String response;
    NightmareContext context;
};
```

Parsed message:

```cpp
struct NightMareMessage
{
    String command;
    String subcommand;
    String args[NM_MAX_ARGS];
    uint8_t argc;
    bool valid;
    String error;
};
```

Limits:

```text
NM_MAX_ARGS         5
NM_MAX_MESSAGE_LEN  512
```

## Command API

```cpp
void setCommandResolver(
    NightMareResults (*resolver)(
        const NightMareMessage &message));

NightMareMessage parseNightMareMessage(
    const String &message);

NightMareMessage parseNightMareMessage2(
    const String &message);

bool ensureSize(
    const String &str,
    size_t maxLength,
    String &error);

NightMareResults handleNightMareCommand(
    const String &message,
    NightmareContext context = NightmareContext());
```

When serial console is enabled:

```cpp
void NightMareCommand_SerialResolver(
    SERIALTYPE *serial,
    char readUntilChar = '\n');
```

## RuntimeState

Global:

```cpp
extern RuntimeState SystemState;
```

Capacity:

```cpp
RuntimeState::MaxEntries == 64
```

Methods:

```cpp
bool set(
    const String &key,
    const String &value);

String get(
    const String &key,
    const String &defaultValue = "") const;

bool setFlag(
    const String &key,
    bool value);

bool getFlag(
    const String &key) const;

bool exists(
    const String &key) const;

bool remove(
    const String &key);

void clear();

size_t size() const;

String toJson() const;
```

## StateStore

When Settings are enabled:

```cpp
extern StateStore PersistentSettings;
```

Constructor:

```cpp
explicit StateStore(bool persistent = true);
```

Methods:

```cpp
bool begin();
bool load();
bool save();

bool set(
    const String &key,
    const String &value) override;

String get(
    const String &key,
    const String &defaultValue = "") const override;

bool exists(
    const String &key) const override;

bool remove(
    const String &key) override;

void clear() override;
void clear(bool saveChanges);

size_t size() const override;
String toJson() const override;
```

Current persistent file:

```text
/configs.json
```

Current JSON load capacity:

```text
4096 bytes
```

## Time

Namespace:

```cpp
NightMare::Time
```

```cpp
constexpr time_t SecondsPerMinute = 60;
constexpr time_t SecondsPerHour = 60 * SecondsPerMinute;

enum TimeStampFormat
{
    DateAndTime,
    OnlyDate,
    SmallDate,
    OnlyTime,
    OnlyTimeWithSeconds,
    OnlyTimeLive,
    DowDate,
    TimeSinceStamp,
    CountdownFromTimestamp
};
```

Clock API:

```cpp
time_t now();
bool valid();
bool setEpoch(time_t epoch);

int second(time_t epoch = now());
int minute(time_t epoch = now());
int hour(time_t epoch = now());
int day(time_t epoch = now());
int month(time_t epoch = now());
int year(time_t epoch = now());
```

The component accessors are UTC.

Local/human formatting:

```cpp
String timestampToDateString(time_t timestamp, TimeStampFormat format = DateAndTime);
String timeString(time_t timestamp = now());
String fullTimeString(time_t timestamp = now());
String dateString(time_t timestamp = now());
time_t timestampOfNextOccurrence(const String &timeString);
```

See [Time](modules/time.md).

## Time synchronization

Canonical synchronization API:

```cpp
bool startSntpTimeSync();
void manualSyncTime(unsigned long timestamp);
void onTimeSync(void (*callback)(void));
void processTimeSyncEvents();
```

`startSntpTimeSync()` starts asynchronous ESP32 SNTP. `processTimeSyncEvents()` is normally called by `tickNightMareESP()`.

The active source currently also contains a deprecated `autoSyncTime()` compatibility alias; new code should use `startSntpTimeSync()`.

## Telemetry

Global:

```cpp
extern TelemetryService Telemetry;
```

Types:

```cpp
enum class InfoType
{
    INVALID,
    INFO,
    IDENTITY,
    HARDWARE,
    BUILD,
    BOOT,
    SYSTEM,
    NETWORK
};

struct TelemetryResult
{
    bool valid;
    String data;
};

enum class HardwareFormat
{
    JSON,
    MSGPACK
};
```

API:

```cpp
InfoType getInfoType(const String &type);

bool Telemetry.start();

TelemetryResult Telemetry.getInfo(
    InfoType type = InfoType::INFO) const;

TelemetryResult Telemetry.getInfo(
    const String &type) const;

bool Telemetry.publishInfo(
    InfoType type = InfoType::INFO);

bool Telemetry.publishInfo(
    const String &type);

TelemetryResult Telemetry.getHardware(
    HardwareFormat format = HardwareFormat::JSON) const;

bool Telemetry.publishHardware(HardwareFormat format);
bool Telemetry.publishHardware();

bool Telemetry.publishAll();
```

## MQTT facade

Broker selectors:

```cpp
constexpr bool LOCAL_MQTT = true;
constexpr bool REMOTE_MQTT = false;
```

Lifecycle/state:

```cpp
void MQTT_Init(
    bool localBroker = REMOTE_MQTT);

void MQTT_End();
void MQTT_Finish();

void MQTT_change_to(bool localBroker);

bool MQTT_isLocal();
bool MQTT_Connected();

int8_t MQTT_State();
String MQTTStateJson();
```

Publish:

```cpp
bool MQTT_Publish(
    const String &topic,
    const String &message,
    bool insertOwner = true,
    bool retained = false);

void MQTT_Send(
    String topic,
    String message,
    bool insertOwner = true,
    bool retained = false);

void MQTT_Send_Raw(
    String topic,
    String message);

bool MQTT_Queue_Async_Message(
    String topic,
    String message,
    bool insertOwner = false,
    bool retained = false);
```

Custom subscriptions:

```cpp
bool MQTT_SubscribeTopic(
    const String &topicFilter);

bool MQTT_UnsubscribeTopic(
    const String &topicFilter);
```

Discovery:

```cpp
bool MQTT_SetDiscovery(bool enabled);
bool MQTT_DiscoveryEnabled();
```

Status helper:

```cpp
String deviceStatusJson(bool online);

String deviceStatusJson(
    const String &deviceName,
    bool online);
```

Project hooks:

```cpp
void MQTT_onMessage(
    void (*cb)(String topic, String message),
    bool onlyDeviceMessages = true);

void MQTT_onConnected(
    void (*cb)(void));

void MQTT_onDisconnected(
    void (*cb)(bool localBroker));
```

Current MQTT implementation limits:

```text
queued async messages:      5
custom subscriptions:       16
custom filter length:       192
incoming MQTT payload:      32768 bytes
MQTT QoS:                   0
```

## WiFi

When enabled:

```cpp
typedef void (*WiFiConnectedCallback)(
    bool firstConnection);

void WiFi_onConnected(
    WiFiConnectedCallback callback);

bool WiFi_Connect(
    const char *ssid,
    const char *password,
    int timeoutMs = 0,
    void *waitCallback(unsigned int) = nullptr);

bool WiFi_ConnectAsync(
    const char *ssid,
    const char *password,
    bool deleteAfterConnect = true);

void WiFi_Disconnect();

bool WiFi_Auto();

void WiFi_Scan();

bool WiFi_ChangeCredentials(
    const String &ssid,
    const String &password);

const char *WiFi_getAuthTypeName(
    wifi_auth_mode_t authType);

const char *WiFi_getStatusName(
    wl_status_t status);
```

## ESP lifecycle

```cpp
void startNightMareESP();
void tickNightMareESP();
```

Normal application structure:

```cpp
void setup()
{
    // Bind Resources and handlers first.
    startNightMareESP();

    // Application hardware/services.
}

void loop()
{
    tickNightMareESP();

    // Application cooperative work.
}
```

## HardwareProfile

```cpp
namespace NMHardware
{
enum class Direction
{
    Input,
    Output,
    Bidirectional,
    Power,
    Ground,
    Bus
};

enum class Pull
{
    None,
    Up,
    Down,
    ExternalUp,
    ExternalDown
};

enum class SignalType
{
    Gpio, SpiClock, SpiMosi, SpiMiso, SpiChipSelect,
    I2cData, I2cClock, UartTransmit, UartReceive,
    Pwm, Analog, OneWire, Power, Ground
};

class Resistor
{
public:
    explicit Resistor(double ohms);
    explicit Resistor(const char *value);
    bool valid() const;
    uint8_t firstDigit() const;
    uint8_t secondDigit() const;
    int8_t exponent() const;
    uint16_t encoded() const;
    double ohms() const;
};

struct Device
{
    const char *id;
    const char *model;
    uint8_t board;
};

struct Board
{
    const char *id;
    const char *model;
};

struct Connection
{
    int16_t pin;
    uint8_t device;
    const char *signal;
    uint8_t bus;
    SignalType type;
    Direction direction;
    Pull pull;
    bool activeLow;
    Resistor resistor;
};

struct Profile
{
    const Board *boards;
    size_t boardCount;
    const Device *devices;
    size_t deviceCount;
    const Connection *connections;
    size_t connectionCount;
};

Profile getProfile();
}
```

A consuming project may provide `NightMareHardware.h` with:

```cpp
NMHardware::Profile projectProfile();
```

## Logging

Levels:

```text
NM_LOG_LEVEL_OFF      0
NM_LOG_LEVEL_ERROR    1
NM_LOG_LEVEL_WARNING  2
NM_LOG_LEVEL_INFO     3
NM_LOG_LEVEL_DEBUG    4
NM_LOG_LEVEL_TRACE    5
```

Configure:

```cpp
#define NM_LOG_LEVEL NM_LOG_LEVEL_DEBUG
```

ANSI formatting:

```cpp
#define NM_LOG_USE_ANSI 1
```

Macros:

```cpp
LOG_ERROR(module, ...)
LOG_WARNING(module, ...)
LOG(module, ...)
LOG_DEBUG(module, ...)
LOG_TRACE(module, ...)

OK_LOG(value)
```

## Feature defaults

Current defaults:

```text
NM_ENABLE_SETTINGS                  1
NM_ENABLE_RESOURCES                 1
NM_ENABLE_NETWORK                   1
NM_ENABLE_CONSOLE                   1
NM_ENABLE_WIFI                      1
NM_ENABLE_MQTT                      1
NM_ENABLE_TELEMETRY                 1
NM_ENABLE_SCHEDULER                 1
NM_ENABLE_JOBS                      1
NM_ENABLE_TIME_SYNC                 1

NM_TIMEZONE                        "UTC0"
NM_NTP_SERVER_1                    "pool.ntp.org"
NM_NTP_SERVER_2                    "time.nist.gov"
NM_NTP_SERVER_3                    "time.google.com"

NM_ENABLE_OTA                       0
NM_ENABLE_HTTP                      0
NM_ENABLE_WEBSOCKET                 0
NM_ENABLE_LVGL                      0

NM_ENABLE_ACTION_PAYLOAD_ASSERTION  0

NM_SCHEDULER_OWN_TASK               1
NM_CONSOLE_BUILTINS                 1
NM_CONSOLE_SERIAL                   0

NM_PLATFORM_ESP32                   1
```

Other defaults:

```text
NM_TELEMETRY_INTERVAL_MS          60000UL
NM_NETWORK_TELEMETRY_INTERVAL_MS 300000UL
NM_IDENTITY_CLEANUP_RETRY_MS      60000UL
NM_FIRMWARE_VERSION              "unspecified"
NM_LOG_LEVEL                     0
NM_LOG_USE_ANSI                  1
```

## Compile-time dependency graph

Current enforced dependencies:

```text
MQTT -> NETWORK
NETWORK -> RESOURCES

TELEMETRY -> SCHEDULER
TELEMETRY -> MQTT

JOBS -> SCHEDULER

SCHEDULER -> SETTINGS
SCHEDULER -> CONSOLE

CONSOLE -> SETTINGS

TIME_SYNC -> WIFI
TIME_SYNC -> SETTINGS

WIFI -> SETTINGS

OTA -> WIFI
OTA -> SETTINGS

HTTP -> NETWORK
HTTP -> CONSOLE

WEBSOCKET -> HTTP

CONSOLE_SERIAL -> CONSOLE
```

See [Known gaps](../architecture/known-gaps.md) for current couplings that may later be separated.

## Current library version

At the documentation revision used for this page, `library.json` reports:

```text
0.2.0
```

Use the MCP `get_version` tool when you need the version and exact revision currently being served rather than relying on a static page.
