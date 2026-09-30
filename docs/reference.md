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

Config-only include:

```cpp
#include <NightMare/Config.h>
```

## Config

Supported types are the built-in `NetCodec<T>` types: `bool`, integral types,
floating-point types, `String`, `TimeType`, and `ColourType`.

```cpp
template <typename T>
class Config
{
public:
    using WriteRequestHandler =
        bool (*)(Config<T> &config, const T &requested);

    Config(
        const String &name,
        const T &defaultValue,
        bool requireReboot = false);

    WriteRequestHandler onWrite;

    const T &value() const;
    bool set(const T &value);
};
```

The firmware default is required and becomes the runtime value at construction.
Construction also registers the object with `configManager()`; destruction
unregisters it. Reboot metadata is the third argument, which keeps
`Config<bool>` unambiguous. A successful startup restore replaces the default
with a valid saved value.

`ConfigManager` invokes `onWrite` with the decoded proposed value after the
optional global change handler and before committing. Returning `false`
rejects the write. Direct local `set()` calls bypass both ingress handlers.
Both paths persist before changing the runtime value and fail without changing
that value if storage cannot be updated.

Metadata available through `ConfigBase`:

```cpp
const String &name() const;
bool requiresReboot() const;
NetValueType type() const;
```

Manager and handler:

```cpp
using ConfigChangeHandler =
    bool (*)(const String &key, const String &value);

ConfigManager &configManager();

bool bind(ConfigBase *config);
bool unbind(ConfigBase *config);
bool restore();
bool restored() const;
String handle(const String &command);
void setChangeHandler(ConfigChangeHandler handler);
bool buildManifestMsgPack(uint8_t *buffer, size_t capacity, size_t &written) const;
String buildManifestBase64() const;
```

`bind()` and `unbind()` remain available for explicit runtime use and retain
their duplicate pointer/name and capacity checks. `restore()` initializes the
settings backend, restores all currently bound Configs, and is idempotent after
success. A Config bound after successful restoration is restored individually
before `bind()` returns; registration is rolled back if that initialization
cannot complete.

`handle("list")` returns JSON objects with `name`, `type`, `require_reboot`,
and `value`. `value` is the Config's current canonical `NetCodec<T>` String
encoding. The binary manifest remains declaration-only.

Limits and manifest versions:

```cpp
ConfigManagerMaxConfigs = 64
ConfigManifestEncodingVersion = 1
ConfigManifestVersion = 1
```

The binary manifest is `[encodingVersion, manifestVersion, configs[]]`; each
entry is `[name, NetValueType, requireReboot]`. `handle("manifest")` returns
Base64 of those exact MessagePack bytes.

Config persistence uses reserved `_config:<name>` keys in
`PersistentSettings` and canonical `NetCodec<T>` text. This storage mapping is
an implementation detail and does not add a manifest field or version change.
`NM_ENABLE_SETTINGS=0` is rejected at compile time because `Config<T>` is
always persistent. Local `set()` rejects values whose encoded representation
cannot be decoded again.

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

Authoritative value mirroring (one per Sensor, last call wins):

```cpp
ManagedSensor<T> &dependsOn(NetValueResource &source);
```

Read back from any Value:

```cpp
const NetValueResource *dependency() const;
```

## NetValue<T>

Common Value metadata:

```cpp
NetValueType type() const;
bool available() const;
ResourceFreshness freshness() const;
uint32_t lastUpdateMs() const;
bool advertisementPolicyKnown() const;
bool advertisementEnabled() const;
uint32_t advertisementPeriodSeconds() const;
const NetValueResource *dependency() const;
```

ManagedSensor and ManagedState expose:

```cpp
const T &getValue() const;
bool setValue(const T &value);
bool setAvailable(bool available);
bool setAdvertisementEnabled(bool enabled);
bool setAdvertisementPeriod(uint32_t seconds);
```

RemoteSensor and RemoteState expose `getValue()`, `hasValue()`, and `isStale()`;
RemoteState also exposes `setValue()`.

Advertisement defaults and limits:

```cpp
NetResourceDefaultAdvertisementPeriodSeconds = 300
NetResourceMinAdvertisementPeriodSeconds = 5
NetResourceMaxAdvertisementPeriodSeconds = 86400
NetResourceAdvertisementRetryMs = 1000
NetResourceManifestRetryMs = 1000
```

Advertisement period `0` is also valid and means event-driven only. Values 1
through 4 are invalid.

## Remote Value source selection

RemoteSensor and RemoteState expose:

```cpp
bool setSource(
    const String &deviceName,
    const String &resourceName);

bool clearSource();
```

The constructor taking one `String` declares the stable local name. Sources are
stored by that name in `/remoteresources.json` and restored by
`startNightMareESP()` before networking starts.

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
    STRUCT,
    TIME,
    COLOUR
};
```

Built-in `NetCodec<T>` support:

```text
String
bool
integral types except bool
floating-point types
TimeType
ColourType
```

`TimeType` API:

```cpp
TimeType();
TimeType(uint8_t hour, uint8_t minute, uint8_t second = 0);
uint8_t hour() const;
uint8_t minute() const;
uint8_t second() const;
String toString() const;
```

Out-of-range constructor components produce `00:00:00`; `decodeTime()` instead
reports failure and leaves its output unchanged.

`ColourType` stores `0xRRGGBBAA` and exposes:

```cpp
ColourType();
explicit ColourType(uint32_t value);
ColourType(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
uint32_t rawValue() const;
NightMare::RGB toRGB() const;
NightMare::RGBA toRGBA() const;
NightMare::HSV toHSV() const;
static ColourType fromHSV(const NightMare::HSV &hsv);
```

The `RGB`, `RGBA`, and `HSV` component structs belong to the `NightMare`
namespace.

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
RemoteAction(const String &localName);

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
bool setSource(
    const String &deviceName,
    const String &resourceName);

bool clearSource();

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
bool loadRemoteSources();
bool loadAdvertisementSettings();
void tick();

bool announceAll();
bool publishManifest();
bool publishConsumeManifest();
bool publishResourceStates();
void subscribeAll();
bool needsSubscription(
    const String &topicFilter) const;

bool handleIngressMessage(
    const String &topic,
    const String &message);

using ManifestHandler =
    void (*)(const String &deviceName, const String &manifest);

void setManifestHandler(ManifestHandler handler);

using EncodedManifestHandler =
    void (*)(const String &deviceName, const String &encoded);

void setEncodedManifestHandler(
    EncodedManifestHandler handler);

static bool decodeManifest(
    const String &encoded,
    JsonDocument &into);

static bool decodeConsumeManifest(
    const String &encoded,
    JsonDocument &into);

constexpr uint8_t ConsumeManifestEncodingVersion = 1;
constexpr uint8_t ConsumeManifestVersion = 2;
constexpr uint8_t ManifestEncodingVersion = 1;
constexpr uint8_t ResourceManifestVersion = 4;

String resolveResourceConsumeManifestTopic(
    const String &deviceName,
    ManifestFormat format = ManifestFormat::JSON);

ActionResult executeAction(
    NetActionResource &action,
    const String &canonicalPayload);

ActionResult executeCommand(
    const String &expression);

// Resource command grammar:
//   >list
//   >manifest [publish] [json|msgpack]
//   >drop <name|owner/name>
//   >raw <topic> [payload]
//   > <name|owner/name> [get|set|invoke|source|enable|period] [payload]

bool withdrawIdentity(const String &oldDeviceName);
```

Connection boundary:

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
Resource manifest payload limit: 16384 bytes
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
    NM_CMD_SRC_ESPNOW,   // console topic delivered by the ESP-NOW gateway
    NM_CMD_ANS_DO_NOT_RESPOND = 0xFF
};
```

Console topics are labelled by the connection that delivered them:
`NM_CMD_SRC_MQTT` for MQTT/LOCAL_MQTT, `NM_CMD_SRC_ESPNOW` for ESP-NOW.

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

Capacity:

```cpp
RuntimeState::MaxEntries == 128
```

The 128-entry store reserves capacity for all 64 Config slots plus 64 entries
of framework/application settings headroom.

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

## SystemState

```cpp
enum class SystemFlag : uint16_t
{
    OtaRunning = 0,
    TimeSynced,
    PersistentStorageReady,
    Count
};

enum class SystemRequest : uint16_t
{
    PublishStatus = 0,
    PublishManifest,
    PublishConsumeManifest,
    PublishResourceStates,
    PublishInfo,
    PublishHardwareJson,
    Count
};

class SystemStateStore
{
public:
    bool get(SystemFlag flag) const;
    void set(SystemFlag flag);
    void clear(SystemFlag flag);

    void request(SystemRequest request);
    bool pending(SystemRequest request) const;
    bool take(SystemRequest request);
};

extern SystemStateStore SystemState;
```

The implementation uses fixed bit banks derived from each `Count`. Operations
are task-safe and allocation-free; there is no ISR-safe API.

Deferred publication processing defaults to:

```cpp
NM_SYSTEM_REQUEST_RETRY_MS == 1000UL
NM_SYSTEM_REQUEST_MAX_RETRY_MS == 300000UL
```

After a failure, the delay doubles from the initial retry interval up to the
five-minute cap. The request moves behind other ready work and remains eligible
for future retries. A new request for the same work clears its existing delay
and runs as fresh work. No retry is attempted while MQTT is offline; a delay
that elapsed during the outage is ready after reconnect.

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

`save()` writes and verifies `/configs.tmp` before atomically renaming it over
the live file, preserving the previous durable document when the temporary
write fails.

Settings loading uses a dynamically sized ArduinoJson 7 `JsonDocument`;
there is no fixed JSON load capacity.

Persistent `set()` rolls back its in-memory mutation when saving fails.
`ConfigManager` uses this store for reserved `_config:<name>` entries.

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
extern Config<bool> HeartbeatEnabled; // heartbeat:enable, default true
extern Config<int> HeartbeatPeriod;   // heartbeat:period, seconds, default 15

constexpr int HeartbeatMinPeriodSeconds = 15;
constexpr int HeartbeatMaxPeriodSeconds = 86400;
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
    NETWORK,
    HEARTBEAT
};

struct TelemetryResult
{
    bool valid;
    String data;
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

TelemetryResult Telemetry.getHardware() const;
bool Telemetry.publishHardware();

bool Telemetry.publishAll();
```

## Network connection

```cpp
namespace NightMare
{
enum class ConnectionType : uint8_t
{
    AUTO = 0,
    MQTT,
    LOCAL_MQTT,
    ESP_NOW
};

enum class ConnectionState : uint8_t
{
    STOPPED,
    DISCOVERING,
    CONNECTING,
    CONNECTED,
    ERROR
};

extern Config<int> preferredConnection;

bool Publish(const char *topic,
             const uint8_t *payload,
             size_t length,
             bool retained = false);
bool Subscribe(const char *topicFilter);
bool Unsubscribe(const char *topicFilter);

bool SelectConnection(ConnectionType connection);
ConnectionType GetSelectedConnection();
ConnectionState GetConnectionState();
}
```

`MQTT` means Remote MQTT/TLS. The payload pointer and explicit length make the
generic boundary binary-safe. `ESP_NOW` and `AUTO` are represented but not yet
implemented; selecting either returns `false` without interrupting the working
MQTT connection.

## MQTT implementation limits

MQTT is an implementation behind `NmConnection`; it does not expose a second
public lifecycle, state, publish, or subscription API. Current implementation
limits are:

```text
queued reconnect messages:  5
connection subscriptions:    256
topic filter length:        192
incoming MQTT payload:      32768 bytes
MQTT QoS:                   0
```

## WiFi radio

When `NM_ENABLE_WIFI_RADIO` (derived: `NM_ENABLE_WIFI || NM_NETWORK_ESPNOW`):

```cpp
// ESP-IDF driver (Network/WiFiRadio/NmWifiRadio.h): the radio and nothing above it.
bool WiFiRadio_start();   // driver init, STA mode, esp_wifi_start; no AP. Idempotent.
void WiFiRadio_stop();    // stop + deinit; the station and ESP-NOW stop with it
bool WiFiRadio_running();
esp_netif_t *WiFiRadio_stationNetif();  // nullptr unless NM_ENABLE_WIFI
uint8_t WiFiRadio_channel();            // 0 while not running
typedef void (*WiFiRadioStateCallback)(bool running);
void WiFiRadio_onState(WiFiRadioStateCallback callback);

// NightMare integration (Network/WiFiRadio/NmWifiRadioService.h)
bool NightMare::WiFiRadioBegin();  // start + report; startNightMareESP() calls it
void NightMare::WiFiRadioEnd();    // stops the station (if any), then the radio
```

The radio is what ESP-NOW needs; it never configures or joins an AP. It is
reported to `NmConnection` as radio availability (`OnRadioAvailabilityIngress`),
which is what starts an ESP-NOW connection. The station netif is created with
the driver, before `esp_wifi_start`, because it follows the driver's STA events
and one created later misses `STA_START`.

## WiFi IP station

When `NM_ENABLE_WIFI`:

```cpp
// ESP-IDF driver (Network/WiFiIP/NmWifiEsp.h): no Arduino, storage or identity.
bool WiFi_start(const NightMare::WiFiProfile &profile, const char *hostname = nullptr);
bool WiFi_changeProfile(const NightMare::WiFiProfile &profile);
void WiFi_stop();

NightMare::WiFiState WiFi_state();  // STOPPED, CONNECTING, CONNECTED, DISCONNECTED
NightMare::WiFiInfo WiFi_info();    // state, ssid, ip, txPower, txPowerDbm, rssi, channel
typedef void (*WiFiStateCallback)(NightMare::WiFiState state);
void WiFi_onState(WiFiStateCallback callback);

bool WiFi_startScan();
bool WiFi_scanInProgress();
int WiFi_scanCount();
bool WiFi_scanResult(size_t index, NightMare::WiFiScanResult &result);

const char *WiFi_getAuthTypeName(wifi_auth_mode_t authType);
const char *WiFi_stateName(NightMare::WiFiState state);
bool WiFi_isValidTxPower(int quarterDbm);

// NightMare integration (Network/WiFiIP/NmWifiService.h)
typedef void (*WiFiConnectedCallback)(bool firstConnection);
void WiFi_onConnected(WiFiConnectedCallback callback);
NightMare::WiFiProfile NightMare::WiFiStoredProfile();
bool NightMare::WiFiBegin();
bool NightMare::WiFiApplyProfile(const NightMare::WiFiProfile &profile);
```

The station is ESP-IDF only, runs on the radio without owning it, and has
three actions and a state.

- `WiFi_start()` starts the radio if needed, then joins the AP in the profile it
  is given and keeps a recovery task running: it retries every 15 seconds while
  cycling through the ESP32 driver's supported transmit-power levels.
- `WiFi_changeProfile()` connects to a new network or applies a TX power. It
  tries the profile for up to 15 seconds and restores the previous one on
  failure. It persists nothing and fails while stopped.
- `WiFi_stop()` stops the station only: it disconnects, clears the station
  config (so ESP-NOW may hop channels again) and ends the recovery task. The
  radio stays up; `WiFiRadio_stop()` turns it off.
- `WiFi_state()` is `STOPPED`, or running as `CONNECTING`, `CONNECTED` or
  `DISCONNECTED`. `WiFi_info()` adds SSID, IP, TX power (including a fallback
  level the driver settled on), RSSI and channel. `WiFi_onState()` fires on
  every change, from the caller or the station's monitor task -- never the ESP
  event task. `WiFi_startScan()` needs the radio, not a station.

Storage, hostname and first-connection services live in `NmWifiService`:
`WiFiBegin()` starts the radio through `WiFiRadioBegin()` (so it is reported),
loads the stored profile (defaulting to `creds.h`), uses the device name as
hostname, starts the station, and on connection starts OTA and SNTP once,
persists a fallback TX power and then calls the `WiFi_onConnected()` callback.
Every station state change is reported to `NmConnection` as IP-link
availability (`OnIpLinkAvailabilityIngress`); Wi-Fi does not start or select
connections. `NmConnection` starts the preferred connection (then the build's
default profile) once what it runs on is available and nothing is running.
`WiFiApplyProfile()` changes the running station and persists on success, or
only persists while stopped.

The implementation is under `Network/WiFiIP/` and uses `esp_wifi` directly.

## ESP lifecycle

```cpp
void startNightMareESP();
void tickNightMareESP();
```

Normal application structure:

```cpp
void setup()
{
    // Declare/bind Configs, Resources, and handlers first.
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
constexpr uint8_t HwConfigVersion = 2;

enum class AssemblyKind
{
    CustomBoard, MarketBoard, Module, SensorProbe,
    Panel, Enclosure, External, Generic
};

enum class ConnectorKind
{
    Header, ScrewTerminal, Jst, Usb, Terminal,
    DirectPin, DirectWire, Generic
};

enum class CanonicalNet
{
    None, Gnd, Vcc, V3v3, V5v,
    AcPhase, AcNeutral, ProtectiveEarth
};

struct Terminal { const char *id; CanonicalNet canonicalNet; const char *name; };
struct ConnectorContact { const char *id; CanonicalNet canonicalNet; const char *name; };

struct Device
{
    const char *id;
    const Terminal *terminals;
    size_t terminalCount;
    const char *name;
    const char *kind;
    const char *model;
    const char *manufacturer;
};

struct Connector
{
    const char *id;
    const ConnectorContact *contacts;
    size_t contactCount;
    const char *name;
    ConnectorKind kind;
    const char *model;
    const char *manufacturer;
};

enum class EndpointKind { DeviceTerminal, ConnectorContact };

struct EndpointRef
{
    const char *assembly;
    EndpointKind kind;
    const char *owner;
    const char *endpoint;
};

struct WireMetadata
{
    const char *color;
    const char *gauge;
    const char *label;
    uint32_t lengthMm;
};

struct Connection
{
    EndpointRef a;
    EndpointRef b;
    WireMetadata wire;
};

struct Assembly;

struct AssemblyMembers
{
    const Assembly *assemblies;
    size_t assemblyCount;
    const Device *devices;
    size_t deviceCount;
    const Connector *connectors;
    size_t connectorCount;
    const Connection *connections;
    size_t connectionCount;
};

struct Assembly
{
    const char *id;
    const char *definition;
    const char *name;
    AssemblyKind kind;
    const char *model;
    const char *manufacturer;
    const char *serialNumber;
    const char *location;
    AssemblyMembers members;
};

struct HardwareDefinition
{
    const char *id;
    AssemblyKind kind;
    const char *name;
    const char *model;
    const char *manufacturer;
    AssemblyMembers members;
};

struct ValidationResult;
struct TopologyGraph;
struct InferredNets;

struct Profile
{
    const char *hostAssembly;
    const HardwareDefinition *definitions;
    size_t definitionCount;
    const Assembly *roots;
    size_t rootCount;
    const Connection *connections;
    size_t connectionCount;
};

ValidationResult validateHwConfig(const Profile &config);
bool buildTopologyGraph(const Profile &config, TopologyGraph &graph,
                        ValidationResult *diagnostics = nullptr);
bool inferNets(const TopologyGraph &graph, InferredNets &nets);
const char *assemblyModel(const Profile &config, const char *absolutePath);
Profile getProfile();

const HardwareDefinition &esp32C3SuperMiniRev1();
const HardwareDefinition &mycroftYControllerRev1();
const HardwareDefinition &ds18b20ProbeDefinition();
const HardwareDefinition &genericRelayModule1Ch();
const HardwareDefinition *standardDefinitions(size_t &count);
}
```

`startNightMareESP()` initializes persistent settings through DeviceIdentity
and immediately restores all currently bound Configs before Scheduler,
telemetry, WiFi, or MQTT startup. Before this restore, Configs contain their
firmware defaults.

The complete declarations, constructors, diagnostic codes, and fixed graph
capacities are in `NightMare/HardwareProfile.h`. The normative semantics and
JSON field contract are in [Hardware configuration v2](hwconfig-v2-model.md).
Canonical identities are exact; `VCC` is not a wildcard for an unknown positive
rail. Effective assemblies receive graph indices, and `GraphNode::assembly`
stores that index rather than a copied path.

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
TELEMETRY -> NETWORK

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
