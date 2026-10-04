#include <NightMareNetwork.h>
#include <Network/NmMessageRouter.h>
#include <Core/DocumentPayload.h>

#include <type_traits>
#include <new>
#include <utility>
#include <LittleFS.h>

namespace
{
RuntimeState smokeState;

template <typename T>
class HasSetValue
{
    template <typename U>
    static auto test(int) -> decltype(std::declval<U &>().setValue(1), std::true_type());
    template <typename>
    static std::false_type test(...);

public:
    static constexpr bool value = decltype(test<T>(0))::value;
};

template <typename T>
class HasFreshnessField
{
    template <typename U>
    static auto test(int) -> decltype((void)std::declval<U &>().freshness, std::true_type());
    template <typename>
    static std::false_type test(...);

public:
    static constexpr bool value = decltype(test<T>(0))::value;
};

template <typename T>
class HasEncodedValue
{
    template <typename U>
    static auto test(int) -> decltype(std::declval<const U &>().encodedValue(), std::true_type());
    template <typename>
    static std::false_type test(...);

public:
    static constexpr bool value = decltype(test<T>(0))::value;
};

template <typename T>
class HasAdvertisementApi
{
    template <typename U>
    static auto test(int) -> decltype(std::declval<U &>().advertisementEnabled(),
                                     std::true_type());
    template <typename>
    static std::false_type test(...);

public:
    static constexpr bool value = decltype(test<T>(0))::value;
};

static_assert(HasSetValue<ManagedSensor<int>>::value, "ManagedSensor must be writable locally");
static_assert(!HasSetValue<RemoteSensor<int>>::value, "RemoteSensor must not expose setValue");
static_assert(HasSetValue<ManagedState<int>>::value, "ManagedState must expose setValue");
static_assert(HasSetValue<RemoteState<int>>::value, "RemoteState must expose setValue");
static_assert(!HasFreshnessField<RemoteSensor<int>>::value,
              "freshness must be exposed through its accessor, not a public field");
static_assert(!HasEncodedValue<ManagedSensor<int>>::value, "wire encoding is framework-internal");
static_assert(!HasAdvertisementApi<ManagedSensor<int>>::value,
              "advertisement policy must remain manager-internal");
static_assert(!std::is_constructible<NetValue<int>, const String &>::value,
              "NetValue is an implementation base, not an application resource");
static_assert(static_cast<uint8_t>(NightMare::ConnectionType::AUTO) == 0,
              "ConnectionType persistence values must remain stable");
static_assert(static_cast<uint8_t>(NightMare::ConnectionType::MQTT) == 1,
              "Remote MQTT is the first concrete connection");
static_assert(std::is_same<decltype(&NightMare::Publish),
                           bool (*)(const char *, const uint8_t *, size_t, bool)>::value,
              "The generic connection publication boundary must remain binary-safe");
static_assert(std::is_same<NightMare::MessageHandler,
                           void (*)(const char *, const uint8_t *, size_t, bool)>::value,
              "The generic message callback boundary must remain binary-safe");
static_assert(std::is_same<decltype(&NightMare::OnMessage),
                           void (*)(NightMare::MessageHandler)>::value,
              "OnMessage must remain part of the public connection API");
static_assert(std::is_same<decltype(&NightMare::WiFiIP_enable), bool (*)()>::value,
              "WiFiIP must expose an independent enable lifecycle");
static_assert(std::is_same<decltype(&NightMare::Mqtt_enable),
                           bool (*)(NightMare::ConnectionType)>::value,
              "MQTT must expose an independent profile-aware lifecycle");
#if NM_NETWORK_ESPNOW
static_assert(std::is_same<decltype(&NightMare::EspNow_suspend),
                           bool (*)(NightMare::ConnectivitySuspendReason)>::value,
              "ESP-NOW must expose scan suspension");
#endif

ManagedSensor<int> managedSensor("managed_sensor");
ManagedSensor<TimeType> managedTime("managed_time");
ManagedSensor<ColourType> managedColour("managed_colour");
RemoteSensor<int> remoteSensor("outside_temperature");
RemoteSensor<int> otherTemperature("temperature", NetDeviceIdentity("inside-node"));
ManagedState<int> managedState("managed_state");
RemoteState<int> remoteState("outside_target");
ManagedAction managedAction("managed_action");
RemoteAction remoteAction("outside_action");

class RecordingPublisher : public ResourcePublisher
{
public:
    bool publish(const String &topic, const String &payload, bool retained) override
    {
        if (topic.endsWith("/state"))
        {
            ++stateAttempts;
            lastStateTopic = topic;
            lastStatePayload = payload;
            stateWasRetained = retained;
            if (failState)
                return false;
            ++statePublishes;
        }
        if (topic.endsWith("/manifest/msgpack") || topic.endsWith("/manifest/json"))
        {
            ++manifestAttempts;
            if (failManifest || (failJsonDocuments && topic.endsWith("/json")))
                return false;
            ++manifestPublishes;
            if (topic.endsWith("/manifest/msgpack"))
                lastManifestPacked = payload;
            else
                lastManifestJson = payload;
        }
        if (topic.endsWith("/manifest/consume/json"))
        {
            ++consumeJsonPublishes;
            if (failJsonDocuments)
                return false;
            lastConsumeJson = payload;
            consumeWasRetained = retained;
        }
        else if (topic.endsWith("/manifest/consume/msgpack"))
        {
            ++consumePackedPublishes;
            lastConsumePacked = payload;
            consumeWasRetained = retained;
        }
        if (topic.endsWith("/manifest") || topic.endsWith("/manifest/consume") ||
            topic.endsWith("/hardware"))
            ++bareDocumentPublishes;
        if (topic.endsWith("/event"))
        {
            ++eventAttempts;
            lastEventTopic = topic;
            lastEventPayload = payload;
            if (retained)
                ++eventRetained;
            if (failEvent)
                return false;
            ++eventPublishes;
        }
        return true;
    }

    int eventAttempts = 0;
    int eventPublishes = 0;
    int eventRetained = 0;
    bool failEvent = false;
    String lastEventTopic;
    String lastEventPayload;
    String lastConsumePacked;

    int consumeJsonPublishes = 0;
    int consumePackedPublishes = 0;
    bool consumeWasRetained = false;
    String lastConsumeJson;
    int stateAttempts = 0;
    int statePublishes = 0;
    bool stateWasRetained = false;
    bool failState = false;
    int manifestAttempts = 0;
    int manifestPublishes = 0;
    bool failManifest = false;
    bool failJsonDocuments = false;
    String lastManifestJson;
    String lastManifestPacked;
    String lastStateTopic;
    String lastStatePayload;
    int bareDocumentPublishes = 0;
};

// Remembers every filter asked for or given back, as "|filter|filter|", so a test
// can ask whether one particular subscription was made or released.
class RecordingSubscriber : public ResourceSubscriber
{
public:
    bool subscribe(const String &filter) override
    {
        subscribed += filter;
        subscribed += '|';
        return true;
    }
    bool unsubscribe(const String &filter) override
    {
        unsubscribed += filter;
        unsubscribed += '|';
        return true;
    }
    void clear()
    {
        subscribed = "|";
        unsubscribed = "|";
    }
    bool subscribedTo(const String &filter) const
    {
        return subscribed.indexOf(String("|") + filter + "|") >= 0;
    }
    bool unsubscribedFrom(const String &filter) const
    {
        return unsubscribed.indexOf(String("|") + filter + "|") >= 0;
    }

    String subscribed = "|";
    String unsubscribed = "|";
};

int localEventCalls = 0;
uint32_t lastLocalEvent = 0;
int stringEventCalls = 0;

void recordLocalEvent(ManagedEvent<uint32_t> &, const uint32_t &payload)
{
    ++localEventCalls;
    lastLocalEvent = payload;
}

void countStringEvent(ManagedEvent<String> &, const String &)
{
    ++stringEventCalls;
}

int eventCallbacks = 0;
uint32_t lastEventSeen = 0;

void recordEvent(RemoteEvent<uint32_t> &, const uint32_t &payload)
{
    ++eventCallbacks;
    lastEventSeen = payload;
}

bool manifestHasResource(JsonVariantConst manifest, const char *name)
{
    for (JsonVariantConst resource : manifest["resources"].as<JsonArrayConst>())
        if (resource["name"].as<String>() == name)
            return true;
    return false;
}

int writeCalls = 0;
int applicationMessageCalls = 0;
String applicationMessageTopic;
uint8_t applicationMessagePayload[3] = {};
size_t applicationMessageLength = 0;
bool applicationMessageRetained = false;

void recordApplicationMessage(const char *topic, const uint8_t *payload,
                              size_t length, bool retained)
{
    ++applicationMessageCalls;
    applicationMessageTopic = topic;
    applicationMessageLength = length;
    applicationMessageRetained = retained;
    for (size_t i = 0; i < length && i < sizeof(applicationMessagePayload); ++i)
        applicationMessagePayload[i] = payload[i];
}

bool acceptStateWrite(ManagedState<int> &, const int &)
{
    ++writeCalls;
    return true;
}

bool hasHardwareDiagnostic(const NMHardware::Profile &profile,
                           NMHardware::DiagnosticCode code)
{
    const NMHardware::ValidationResult validation =
        NMHardware::validateHwConfig(profile);
    for (size_t i = 0; i < validation.diagnosticCount; ++i)
        if (validation.diagnostics[i].code == code) return true;
    return false;
}

bool graphNodeMatches(const NMHardware::TopologyGraph &graph, size_t nodeIndex,
                      const char *assembly, const char *owner, const char *endpoint)
{
    if (nodeIndex >= graph.nodeCount) return false;
    const NMHardware::GraphNode &node = graph.nodes[nodeIndex];
    return node.assembly < graph.assemblyCount &&
           graph.assemblies[node.assembly].path == assembly &&
           !strcmp(node.owner, owner) && !strcmp(node.endpoint, endpoint);
}
}

void setup()
{
    PersistentSettings.begin();
    PersistentSettings.setFlag("smoke", true);
    smokeState.setFlag("smoke", PersistentSettings.getFlag("smoke"));
    const bool heartbeatDefaults =
        configManager().handle("get heartbeat:enable") == "true" &&
        configManager().handle("get heartbeat:period") == "15";
    const bool heartbeatBounds =
        configManager().handle("set heartbeat:period 14") == "ERROR: change rejected" &&
        HeartbeatPeriod.value() == 15 &&
        configManager().handle("set heartbeat:period 86400") == "OK" &&
        HeartbeatPeriod.value() == 86400 &&
        configManager().handle("set heartbeat:period 86401") == "ERROR: change rejected" &&
        HeartbeatPeriod.value() == 86400 &&
        configManager().handle("set heartbeat:period 15") == "OK";
    const bool heartbeatToggle =
        configManager().handle("set heartbeat:enable false") == "OK" &&
        !HeartbeatEnabled.value() &&
        configManager().handle("set heartbeat:enable true") == "OK" &&
        HeartbeatEnabled.value();
    smokeState.setFlag("heartbeat_config", heartbeatDefaults && heartbeatBounds &&
                                                heartbeatToggle);
    const bool wifiBeforeTransport = NightMare::WiFiIP_enabled();
    const bool mqttBeforeTransport = NightMare::Mqtt_enabled();
#if NM_NETWORK_ESPNOW
    const bool espNowBeforeTransport = NightMare::EspNow_enabled();
#endif
    const NightMareResults networkGet = handleNightMareCommand("NETWORK GET");
    const NightMareResults mqttDependency =
        handleNightMareCommand("NETWORK MQTT ENABLE MQTT");
    const NightMareResults preferenceOnly =
        handleNightMareCommand("NETWORK TRANSPORT SET MQTT");
    const NightMareResults oldSet = handleNightMareCommand("NETWORK SET MQTT");
    const NightMareResults oldIp = handleNightMareCommand("NETWORK IP");
    const bool connectivityCommands =
        networkGet.result && networkGet.response.indexOf("\"transport\"") >= 0 &&
        networkGet.response.indexOf("\"wifi_radio\"") >= 0 &&
        networkGet.response.indexOf("\"wifi_ip\"") >= 0 &&
        networkGet.response.indexOf("\"esp_now\"") >= 0 &&
        networkGet.response.indexOf("\"mqtt\"") >= 0 &&
        !mqttDependency.result &&
        mqttDependency.response.indexOf("wifi_ip_disabled") >= 0 &&
        preferenceOnly.result &&
        NightMare::WiFiIP_enabled() == wifiBeforeTransport &&
        NightMare::Mqtt_enabled() == mqttBeforeTransport &&
#if NM_NETWORK_ESPNOW
        NightMare::EspNow_enabled() == espNowBeforeTransport &&
#endif
        !oldSet.result && oldSet.response.indexOf("unknown_network_command") >= 0 &&
        !oldIp.result && oldIp.response.indexOf("unknown_network_command") >= 0;
    smokeState.setFlag("connectivity_commands", connectivityCommands);
    managedState.onWrite = acceptStateWrite;
    const bool localStateUsesWritePolicy = managedState.setValue(7) && writeCalls == 1 &&
                                           managedState.getValue() == 7;
    managedSensor.setValue(3);
    managedTime.setValue(TimeType(8, 30, 15));
    managedColour.setValue(ColourType(255, 0, 0));
    remoteSensor.setSource("outside-node", "temperature");
    remoteState.setSource("outside-node", "target");
    remoteAction.setSource("outside-node", "remote_action");
    gResourcesManager.bindResource(&managedSensor);
    gResourcesManager.bindResource(&managedTime);
    gResourcesManager.bindResource(&managedColour);
    gResourcesManager.bindResource(&remoteSensor);
    gResourcesManager.bindResource(&otherTemperature);
    gResourcesManager.bindResource(&managedState);
    gResourcesManager.bindResource(&remoteState);
    gResourcesManager.bindResource(&managedAction);
    gResourcesManager.bindResource(&remoteAction);
    NightMare::OnMessage(recordApplicationMessage);
    String binaryPayload;
    binaryPayload.concat("A\0B", 3);
    const bool applicationMessageRouted =
        NmMessageRouter::handleMessage("application/binary", binaryPayload, true);
    const int callsBeforeFrameworkMessage = applicationMessageCalls;
    const bool remoteStateRouted = NmMessageRouter::handleMessage(
        "outside-node/resource/temperature/state", "18", true);
    const bool otherStateRouted = NmMessageRouter::handleMessage(
        "inside-node/resource/temperature/state", "24", true);
    NightMare::OnMessage(nullptr);
    const bool noHandlerDeclines =
        !NmMessageRouter::handleMessage("application/unhandled", "value", false);
    const bool genericMessageIngress = applicationMessageRouted && remoteStateRouted &&
        otherStateRouted && noHandlerDeclines && applicationMessageCalls == 1 &&
        callsBeforeFrameworkMessage == 1 && applicationMessageTopic == "application/binary" &&
        applicationMessageLength == 3 && applicationMessagePayload[0] == 'A' &&
        applicationMessagePayload[1] == 0 && applicationMessagePayload[2] == 'B' &&
        applicationMessageRetained;
    smokeState.setFlag("generic_message_ingress", genericMessageIngress);

    JsonDocument consumePacked;
    JsonArray consumeRoot = consumePacked.to<JsonArray>();
    consumeRoot.add(ConsumeManifestEncodingVersion);
    consumeRoot.add(ConsumeManifestVersion);
    JsonArray consumeItems = consumeRoot.add<JsonArray>();
    JsonArray consumeValue = consumeItems.add<JsonArray>();
    consumeValue.add(static_cast<uint8_t>(NetResourceType::VALUE));
    consumeValue.add("outside_temperature");
    consumeValue.add(true);
    consumeValue.add("outside-node");
    consumeValue.add("temperature");
    consumeValue.add(static_cast<uint8_t>(AccessPolicy::READ));
    consumeValue.add(static_cast<uint8_t>(NetValueType::INTEGER));
    String encodedConsume;
    serializeMsgPack(consumePacked, encodedConsume);
    JsonDocument decodedConsume;
    const bool consumeCodecWorks =
        ResourcesManager::decodeConsumeManifest(encodedConsume, decodedConsume) &&
        decodedConsume["version"].as<int>() == ConsumeManifestVersion &&
        decodedConsume["remotes"][0]["device"].as<String>() == "outside-node" &&
        decodedConsume["remotes"][0]["bound"].as<bool>() &&
        decodedConsume["consumes"].isNull() &&
        resolveResourceConsumeManifestTopic("smoke", DocumentFormat::MSGPACK) ==
            "smoke/manifest/consume/msgpack" &&
        resolveResourceManifestTopic("smoke", DocumentFormat::JSON) ==
            "smoke/manifest/json" &&
        resolveDocumentTopic("smoke", "hardware", DocumentFormat::MSGPACK) ==
            "smoke/hardware/msgpack";

    ResourcesManager consumeManager;
    RecordingPublisher consumePublisher;
    RemoteSensor<float> consumed("temperature", NetDeviceIdentity("weather-node"));
    consumeManager.setPublisher(&consumePublisher);
    const bool consumeBound = consumeManager.bindResource(&consumed);
    JsonDocument boundConsume;
    const bool boundPublished = ResourcesManager::decodeConsumeManifest(
        consumePublisher.lastConsumePacked, boundConsume) &&
        boundConsume["remotes"][0]["bound"].as<bool>() &&
        boundConsume["remotes"][0]["device"].as<String>() == "weather-node";
    consumed.setSource("relay-node", "target");
    JsonDocument retargetConsume;
    const bool retargetPublished = ResourcesManager::decodeConsumeManifest(
        consumePublisher.lastConsumePacked, retargetConsume) &&
        retargetConsume["remotes"][0]["device"].as<String>() == "relay-node";
    consumeManager.unbindResource(&consumed);
    JsonDocument unboundConsume;
    const bool unbindPublished = ResourcesManager::decodeConsumeManifest(
        consumePublisher.lastConsumePacked, unboundConsume) &&
        unboundConsume["remotes"].size() == 0;
    const bool consumeLifecycleWorks = consumeBound && boundPublished && retargetPublished &&
                                       unbindPublished && consumePublisher.consumeWasRetained &&
                                       consumePublisher.consumePackedPublishes >= 4 &&
                                       consumePublisher.bareDocumentPublishes == 0 &&
                                       consumePublisher.consumeJsonPublishes ==
                                           (NM_ENABLE_JSON_WIRE ? consumePublisher.consumePackedPublishes : 0);

    const ActionResult list = gResourcesManager.executeCommand("list");
    const ActionResult local = gResourcesManager.executeCommand(" outside_temperature");
    const ActionResult qualified =
        gResourcesManager.executeCommand(" outside-node/temperature");
    const ActionResult timeValue = gResourcesManager.executeCommand(" managed_time");
    const ActionResult colourValue = gResourcesManager.executeCommand(" managed_colour");
    const ActionResult customManifest = gResourcesManager.executeCommand("manifest json");
    const bool resourceCommandsWork = list.success && list.result.startsWith("TYPE") &&
                                      list.result.indexOf("VALUE") >= 0 &&
                                      list.result.indexOf("outside_temperature") >= 0 &&
                                      local.success && local.result == "18" &&
                                      qualified.success && qualified.result == "18";
    const NightMareResults invalidResourceAdvertisement =
        handleNightMareCommand("> managed_sensor advertise nope");
    const NightMareResults missingResource =
        handleNightMareCommand("> missing_resource");
    const bool resourceErrorsAreExternal =
        !invalidResourceAdvertisement.result &&
        invalidResourceAdvertisement.response ==
            "ERROR: ADVERTISE expects signed milliseconds" &&
        !missingResource.result && missingResource.response == "ERROR: Resource not found";
    const bool customResourcesWork = managedTime.type() == NetValueType::TIME &&
                                     managedColour.type() == NetValueType::COLOUR &&
                                     timeValue.success && timeValue.result == "08:30:15" &&
                                     colourValue.success && colourValue.result == "4278190335" &&
                                     customManifest.success &&
                                     customManifest.result.indexOf("\"type\":\"time\"") >= 0 &&
                                     customManifest.result.indexOf("\"type\":\"colour\"") >= 0 &&
                                     customManifest.result.indexOf(
                                         "\"advertise_ms\":300000") >= 0;

    ResourcesManager advertisementManager;
    RecordingPublisher advertisementPublisher;
    ManagedSensor<int> advertised("advertisement:primary");
    ManagedSensor<int> flatAdvertisement("flat_advertisement");
    advertisementManager.setPublisher(&advertisementPublisher);
    const bool advertisementBound = advertisementManager.bindResource(&advertised) &&
                                    advertisementManager.bindResource(&flatAdvertisement);
    JsonDocument legacySettings;
    legacySettings["advertisement:primary"]["enabled"] = true;
    legacySettings["advertisement:primary"]["period"] = 5;
    File legacyFile = LittleFS.open("/resourcesettings.json", "w");
    const bool legacyWritten = legacyFile && serializeJson(legacySettings, legacyFile) != 0;
    legacyFile.close();
    const bool legacyMigrated = legacyWritten && advertisementManager.loadResourceSettings() &&
        advertisementManager.executeCommand(" advertisement:primary advertise").result == "5000";
    const int beforeInitialState = advertisementPublisher.statePublishes;
    const bool immediateAdvertisement = advertised.setValue(10) &&
        advertisementPublisher.statePublishes == beforeInitialState + 1 &&
        advertisementPublisher.lastStatePayload == "10";
    const ActionResult periodSet =
        advertisementManager.executeCommand(" advertisement:primary advertise 5000");
    const ActionResult disabled =
        advertisementManager.executeCommand(" advertisement:primary advertise -1");
    const int afterWithdrawal = advertisementPublisher.statePublishes;
    const bool disabledStillComputes = disabled.success &&
        advertisementPublisher.lastStatePayload.length() == 0 && advertised.setValue(11) &&
        advertised.getValue() == 11 &&
        advertisementPublisher.statePublishes == afterWithdrawal;
    const ActionResult enabled =
        advertisementManager.executeCommand(" advertisement:primary advertise 5000");
    const bool enabledAdvertised = enabled.success &&
        advertisementPublisher.lastStatePayload == "11";
    const bool unavailableStillComputes = advertised.setAvailable(false) &&
        !advertised.available() && advertisementPublisher.lastStatePayload.length() == 0 &&
        advertised.setValue(12) && advertised.getValue() == 12;
    const bool availabilityRecovered = advertised.setAvailable(true) &&
        advertised.available() && advertisementPublisher.lastStatePayload == "12";

    JsonDocument ownerManifest;
    JsonArray ownerRoot = ownerManifest.to<JsonArray>();
    ownerRoot.add(ManifestEncodingVersion);
    ownerRoot.add(ResourceManifestVersion);
    JsonArray ownerItems = ownerRoot.add<JsonArray>();
    JsonArray ownerValue = ownerItems.add<JsonArray>();
    ownerValue.add(static_cast<uint8_t>(NetResourceType::VALUE));
    ownerValue.add("temperature");
    ownerValue.add(static_cast<uint8_t>(AccessPolicy::READ));
    ownerValue.add(static_cast<uint8_t>(NetValueType::INTEGER));
    ownerValue.add(nullptr);
    ownerValue.add(5000);
    ownerValue.add(nullptr);
    String encodedOwnerManifest;
    serializeMsgPack(ownerManifest, encodedOwnerManifest);
    ResourcesManager freshnessManager;
    RemoteSensor<int> freshnessRemote("temperature", NetDeviceIdentity("freshness-node"));
    const bool freshnessBound = freshnessManager.bindResource(&freshnessRemote);
    freshnessManager.handleIngressMessage("freshness-node/manifest/msgpack",
                                          encodedOwnerManifest);
    freshnessManager.handleIngressMessage("freshness-node/resource/temperature/state", "20");
    const bool remoteFresh = freshnessRemote.available() &&
        freshnessRemote.freshness() == ResourceFreshness::FRESH;
    freshnessManager.handleIngressMessage("freshness-node/resource/temperature/state", "");
    const bool remoteUnavailable = !freshnessRemote.available() &&
        freshnessRemote.freshness() == ResourceFreshness::FRESH;
    freshnessManager.handleIngressMessage("freshness-node/resource/temperature/state", "21");

    delay(10100);
    freshnessManager.tick();
    const bool remoteAged = freshnessRemote.available() &&
        freshnessRemote.freshness() == ResourceFreshness::STALE;
    freshnessManager.handleIngressMessage("freshness-node/resource/temperature/state", "22");
    const bool remoteRefreshed = freshnessRemote.freshness() == ResourceFreshness::FRESH;

    ownerValue[5] = -1;
    encodedOwnerManifest = "";
    serializeMsgPack(ownerManifest, encodedOwnerManifest);
    freshnessManager.handleIngressMessage("freshness-node/manifest/msgpack",
                                          encodedOwnerManifest);
    const bool metadataDisablePreservesAvailability = freshnessRemote.available();
    ownerValue[5] = 5000;
    encodedOwnerManifest = "";
    serializeMsgPack(ownerManifest, encodedOwnerManifest);
    freshnessManager.handleIngressMessage("freshness-node/manifest/msgpack",
                                          encodedOwnerManifest);
    const bool metadataEnablePreservesAvailability = freshnessRemote.available();
    freshnessManager.handleIngressMessage("freshness-node/resource/temperature/state", "23");
    ownerValue[5] = 0;
    encodedOwnerManifest = "";
    serializeMsgPack(ownerManifest, encodedOwnerManifest);
    freshnessManager.handleIngressMessage("freshness-node/manifest/msgpack",
                                          encodedOwnerManifest);
    const bool eventDrivenPolicyApplied = freshnessRemote.available();

    advertisementManager.executeCommand(" flat_advertisement advertise 5000");
    flatAdvertisement.setValue(1);
    const int beforeBoundedTick = advertisementPublisher.stateAttempts;
    advertisementManager.tick();
    const bool boundedTick = advertisementPublisher.stateAttempts == beforeBoundedTick + 1 &&
        advertisementPublisher.lastStatePayload == "12";
    const int afterRefresh = advertisementPublisher.stateAttempts;
    advertisementManager.tick();
    const bool normalPublicationResetTimer =
        advertisementPublisher.stateAttempts == afterRefresh;
    delay(5100);
    advertisementPublisher.failState = true;
    const int beforeFailedRefresh = advertisementPublisher.stateAttempts;
    advertisementManager.tick();
    advertisementManager.tick();
    advertisementManager.tick();
    advertisementManager.tick();
    const bool refreshRateLimited =
        advertisementPublisher.stateAttempts == beforeFailedRefresh + 2;
    advertisementPublisher.failState = false;
    const int beforeFailedRefreshRetry = advertisementPublisher.statePublishes;
    delay(1100);
    advertisementManager.tick();
    const bool failedRefreshRetried =
        advertisementPublisher.statePublishes == beforeFailedRefreshRetry + 1;

    freshnessManager.tick();
    const bool eventDrivenDoesNotAge =
        freshnessRemote.freshness() == ResourceFreshness::FRESH;

    advertisementPublisher.failManifest = true;
    const int beforeManifestFailure = advertisementPublisher.manifestAttempts;
    const ActionResult eventDrivenSet =
        advertisementManager.executeCommand(" advertisement:primary advertise 0");
    const bool manifestFailureRecorded = eventDrivenSet.success &&
        advertisementPublisher.manifestAttempts == beforeManifestFailure + 1;
    advertisementPublisher.failManifest = false;
    delay(1100);
    advertisementManager.tick();
    const bool dirtyManifestRetried =
        advertisementPublisher.manifestAttempts == beforeManifestFailure + 3;

    advertisementManager.unbindResource(&advertised);
    ResourcesManager restoredAdvertisementManager;
    ManagedSensor<int> restoredAdvertisement("advertisement:primary");
    const bool restoredAdvertisementPolicy =
        restoredAdvertisementManager.bindResource(&restoredAdvertisement) &&
        restoredAdvertisementManager.loadResourceSettings() &&
        restoredAdvertisementManager.executeCommand(
            " advertisement:primary advertise").result == "0";

    ResourcesManager hardwareManager;
    RecordingPublisher hardwarePublisher;
    ManagedSensor<int> plainSensor("plain_sensor");
    ManagedSensor<int> hardwareSensor("hardware_sensor");
    ManagedSensor<int> noDisableSensor("no_disable_sensor");
    HardwarePolicy fullPolicy;
    fullPolicy.pollMs = 1000;
    fullPolicy.flags = REPORT_HW_CONNECTION | CONFIGURABLE_POLL | CAN_DISABLE;
    fullPolicy.note = "Address: 0x48";
    HardwarePolicy noDisablePolicy;
    noDisablePolicy.pollMs = 2000;
    noDisablePolicy.flags = CONFIGURABLE_POLL;
    HardwarePolicy longNotePolicy;
    for (size_t i = 0; i <= NetResourceHardwareNoteMaxLength; ++i)
        longNotePolicy.note += 'x';
    const bool hardwareDeclared = hardwareSensor.setHardwarePolicy(fullPolicy) &&
        noDisableSensor.setHardwarePolicy(noDisablePolicy) &&
        !plainSensor.setHardwarePolicy(longNotePolicy);
    hardwareManager.setPublisher(&hardwarePublisher);
    const bool hardwareBound = hardwareManager.bindResource(&plainSensor) &&
        hardwareManager.bindResource(&hardwareSensor) &&
        hardwareManager.bindResource(&noDisableSensor);
    const bool connectionDefaultsConnected = hardwareSensor.hardwareConnected();
    const bool connectionReporting = hardwareSensor.setHardwareConnected(true) &&
        hardwareSensor.hardwareConnected() && !noDisableSensor.setHardwareConnected(true);
    const ActionResult pollSet = hardwareManager.executeCommand(" hardware_sensor poll 5000");
    ResourcesManager restoredHardwareManager;
    ManagedSensor<int> restoredHardware("hardware_sensor");
    restoredHardware.setHardwarePolicy(fullPolicy);
    const bool pollOverrideRestored = restoredHardwareManager.bindResource(&restoredHardware) &&
        restoredHardwareManager.loadResourceSettings() &&
        restoredHardware.hardwarePollMs() == 5000;
    const ActionResult pollDisable = hardwareManager.executeCommand(" hardware_sensor poll -1");
    const ActionResult disableRejected =
        hardwareManager.executeCommand(" no_disable_sensor poll -1");
    const ActionResult pollReset = hardwareManager.executeCommand(" hardware_sensor poll reset");
    const ActionResult hardwareManifest = hardwareManager.executeCommand("manifest json");
    const ActionResult packedHardwareManifest =
        hardwareManager.executeCommand("manifest msgpack");
    JsonDocument decodedHardwareManifest;
    const bool packedHardwareDecoded = packedHardwareManifest.success &&
        ResourcesManager::decodeManifest(hardwarePublisher.lastManifestPacked,
                                         decodedHardwareManifest) &&
        decodedHardwareManifest["version"].as<int>() == ResourceManifestVersion &&
        decodedHardwareManifest["resources"][1]["hardware"]["note"].as<String>() ==
            "Address: 0x48";
    const bool hardwarePolicyWorks = hardwareDeclared && hardwareBound &&
        connectionDefaultsConnected && connectionReporting &&
        pollSet.success && pollOverrideRestored && pollDisable.success &&
        !hardwareSensor.hardwareEnabled() &&
        !disableRejected.success && disableRejected.result == "POLL cannot disable this hardware" &&
        pollReset.success && hardwareSensor.hardwareEnabled() &&
        hardwareSensor.hardwarePollMs() == 1000 &&
        hardwareManifest.success && packedHardwareDecoded &&
        hardwareManifest.result.indexOf("\"poll_ms\":1000") >= 0 &&
        hardwareManifest.result.indexOf("\"flags\":7") >= 0 &&
        hardwareManifest.result.indexOf("\"connected\":true") >= 0 &&
        hardwareManifest.result.indexOf("\"note\":\"Address: 0x48\"") >= 0 &&
        hardwareManifest.result.indexOf("\"name\":\"plain_sensor\",\"kind\":\"value\",\"access\":\"read\",\"type\":\"integer\",\"advertise_ms\":300000,\"hardware\"") < 0;

    const bool advertisementLifecycle = advertisementBound && immediateAdvertisement &&
        legacyMigrated && periodSet.success &&
        disabledStillComputes && enabledAdvertised &&
        unavailableStillComputes && availabilityRecovered && freshnessBound &&
        remoteFresh && remoteUnavailable && remoteAged && remoteRefreshed &&
        metadataDisablePreservesAvailability && metadataEnablePreservesAvailability &&
        eventDrivenPolicyApplied && eventDrivenDoesNotAge &&
        boundedTick && normalPublicationResetTimer && refreshRateLimited &&
        failedRefreshRetried && manifestFailureRecorded && dirtyManifestRetried &&
        restoredAdvertisementPolicy && hardwarePolicyWorks &&
        advertisementPublisher.stateWasRetained;
    smokeState.setFlag("resource_api", localStateUsesWritePolicy &&
                                            managedSensor.name() == "managed_sensor" &&
                                            managedSensor.owner().length() != 0 &&
                                            managedSensor.kind() == NetResourceType::VALUE &&
                                            managedSensor.type() == NetValueType::INTEGER &&
                                            !managedSensor.isRemote() && remoteSensor.isRemote() &&
                                            remoteSensor.hasValue() && !remoteSensor.isStale() &&
                                            resourceCommandsWork && consumeCodecWorks &&
                                            resourceErrorsAreExternal && consumeLifecycleWorks &&
                                            customResourcesWork &&
                                            advertisementLifecycle);
    const String timezone = gDeviceIdentity.getTimezone();
    const NightMareResults timezoneQuery = handleNightMareCommand("TIMEZONE");
    const NightMareResults timezoneSet =
        handleNightMareCommand(String("TIMEZONE SET \"") + timezone + "\"");
    const NightMareResults invalidAdopt = handleNightMareCommand("CHANGE NAME all");
    smokeState.setFlag("identity_commands",
                        timezoneQuery.result && timezoneQuery.response.indexOf(timezone) >= 0 &&
                            timezoneSet.result && gDeviceIdentity.getTimezone() == timezone &&
                            !invalidAdopt.result);
    const NMHardware::Profile profile = NMHardware::getProfile();
    static const NMHardware::ValidationResult hardwareValidation =
        NMHardware::validateHwConfig(profile);
    size_t standardDefinitionCount = 0;
    const NMHardware::HardwareDefinition *standardDefinitions =
        NMHardware::standardDefinitions(standardDefinitionCount);
    static const NMHardware::Assembly catalogRoots[] = {
        {"mycroft", "mycroft-y-controller-rev1"},
        {"catalog_probe", "ds18b20-waterproof-probe"},
        {"catalog_relay", "generic-relay-module-1ch"},
    };
    const NMHardware::Profile catalogProfile{
        "mycroft/esp32", standardDefinitions, standardDefinitionCount,
        catalogRoots, 3, nullptr, 0};
    const bool standardDefinitionsValid =
        NMHardware::validateHwConfig(catalogProfile).valid();
    static const NMHardware::Connection illegalBoundary[] = {
        {{"controller", NMHardware::EndpointKind::DeviceTerminal, "mcu", "GPIO4"},
         {"probe", NMHardware::EndpointKind::DeviceTerminal, "sensor", "DATA"}},
    };
    const NMHardware::Profile illegalBoundaryProfile{
        profile.hostAssembly, profile.definitions, profile.definitionCount,
        profile.roots, profile.rootCount, illegalBoundary, 1};
    const bool boundaryRejected = hasHardwareDiagnostic(
        illegalBoundaryProfile,
        NMHardware::DiagnosticCode::CrossAssemblyDeviceConnection);
    static const NMHardware::Connection canonicalConflict[] = {
        {{"controller", NMHardware::EndpointKind::ConnectorContact, "j_temp", "VDD"},
         {"controller", NMHardware::EndpointKind::ConnectorContact, "j_temp", "GND"}},
    };
    const NMHardware::Profile canonicalConflictProfile{
        profile.hostAssembly, profile.definitions, profile.definitionCount,
        profile.roots, profile.rootCount, canonicalConflict, 1};
    const bool canonicalConflictRejected = hasHardwareDiagnostic(
        canonicalConflictProfile, NMHardware::DiagnosticCode::CanonicalNetConflict);

    static const NMHardware::Assembly validRoot[] = {{"root"}};
    static const NMHardware::Assembly nullIdChildren[] = {
        {"child", "referenced-definition"}};
    static const NMHardware::HardwareDefinition nullIdDefinitions[] = {
        {nullptr, NMHardware::AssemblyKind::Generic, nullptr, nullptr, nullptr,
         {nullIdChildren, 1}}};
    const NMHardware::Profile nullIdProfile{
        "root", nullIdDefinitions, 1, validRoot, 1, nullptr, 0};
    const bool nullIdRejected = hasHardwareDiagnostic(
        nullIdProfile, NMHardware::DiagnosticCode::InvalidId);

    static const NMHardware::Assembly invalidIdRoot[] = {{"bad/root"}};
    const NMHardware::Profile invalidIdProfile{
        "bad/root", nullptr, 0, invalidIdRoot, 1, nullptr, 0};
    const bool invalidIdRejected = hasHardwareDiagnostic(
        invalidIdProfile, NMHardware::DiagnosticCode::InvalidId);

    static const NMHardware::Assembly unknownDefinitionRoot[] = {
        {"root", "does-not-exist"}};
    const NMHardware::Profile unknownDefinitionProfile{
        "root", nullptr, 0, unknownDefinitionRoot, 1, nullptr, 0};
    const bool unknownDefinitionRejected = hasHardwareDiagnostic(
        unknownDefinitionProfile, NMHardware::DiagnosticCode::UnknownDefinition);

    static const NMHardware::Assembly cycleAChildren[] = {{"b", "cycle-b"}};
    static const NMHardware::Assembly cycleBChildren[] = {{"a", "cycle-a"}};
    static const NMHardware::HardwareDefinition cycleDefinitions[] = {
        {"cycle-a", NMHardware::AssemblyKind::Generic, nullptr, nullptr, nullptr,
         {cycleAChildren, 1}},
        {"cycle-b", NMHardware::AssemblyKind::Generic, nullptr, nullptr, nullptr,
         {cycleBChildren, 1}},
    };
    static const NMHardware::Assembly cycleRoot[] = {{"root", "cycle-a"}};
    const NMHardware::Profile cycleProfile{
        "root", cycleDefinitions, 2, cycleRoot, 1, nullptr, 0};
    const bool cycleRejected = hasHardwareDiagnostic(
        cycleProfile, NMHardware::DiagnosticCode::DefinitionCycle);

    static const NMHardware::Device duplicateDefinitionDevices[] = {{"duplicate"}};
    static const NMHardware::HardwareDefinition duplicateMemberDefinitions[] = {
        {"duplicate-members", NMHardware::AssemblyKind::Generic,
         nullptr, nullptr, nullptr,
         {nullptr, 0, duplicateDefinitionDevices, 1}}};
    static const NMHardware::Device duplicateInstanceDevices[] = {{"duplicate"}};
    static const NMHardware::Assembly duplicateMemberRoot[] = {
        {"root", "duplicate-members", nullptr, NMHardware::AssemblyKind::Generic,
         nullptr, nullptr, nullptr, nullptr,
         {nullptr, 0, duplicateInstanceDevices, 1}}};
    const NMHardware::Profile duplicateMemberProfile{
        "root", duplicateMemberDefinitions, 1, duplicateMemberRoot, 1, nullptr, 0};
    const bool duplicateMemberRejected = hasHardwareDiagnostic(
        duplicateMemberProfile, NMHardware::DiagnosticCode::DuplicateMember);

    static const NMHardware::Connection unresolvedEndpoints[] = {
        {{"controller", NMHardware::EndpointKind::DeviceTerminal, "mcu", "missing"},
         {"controller", NMHardware::EndpointKind::ConnectorContact, "j_temp", "DATA"}},
        {{"controller", NMHardware::EndpointKind::DeviceTerminal, "mcu", "GPIO4"},
         {"controller", NMHardware::EndpointKind::ConnectorContact, "j_temp", "missing"}},
    };
    const NMHardware::Profile unresolvedProfile{
        profile.hostAssembly, profile.definitions, profile.definitionCount,
        profile.roots, profile.rootCount, unresolvedEndpoints, 2};
    const bool unresolvedTerminalRejected = hasHardwareDiagnostic(
        unresolvedProfile, NMHardware::DiagnosticCode::TerminalNotFound);
    const bool unresolvedContactRejected = hasHardwareDiagnostic(
        unresolvedProfile, NMHardware::DiagnosticCode::ContactNotFound);

    static const NMHardware::Connection duplicateConnections[] = {
        {{"controller", NMHardware::EndpointKind::ConnectorContact, "j_temp", "VDD"},
         {"controller", NMHardware::EndpointKind::ConnectorContact, "j_temp", "DATA"}},
        {{"controller", NMHardware::EndpointKind::ConnectorContact, "j_temp", "DATA"},
         {"controller", NMHardware::EndpointKind::ConnectorContact, "j_temp", "VDD"}},
    };
    const NMHardware::Profile duplicateConnectionProfile{
        profile.hostAssembly, profile.definitions, profile.definitionCount,
        profile.roots, profile.rootCount, duplicateConnections, 2};
    const bool duplicateConnectionRejected = hasHardwareDiagnostic(
        duplicateConnectionProfile, NMHardware::DiagnosticCode::DuplicateConnection);

    static const NMHardware::Connection selfConnection[] = {
        {{"controller", NMHardware::EndpointKind::ConnectorContact, "j_temp", "DATA"},
         {"controller", NMHardware::EndpointKind::ConnectorContact, "j_temp", "DATA"}},
    };
    const NMHardware::Profile selfConnectionProfile{
        profile.hostAssembly, profile.definitions, profile.definitionCount,
        profile.roots, profile.rootCount, selfConnection, 1};
    const bool selfConnectionRejected = hasHardwareDiagnostic(
        selfConnectionProfile, NMHardware::DiagnosticCode::SelfConnection);

    static char capacityIds[NMHardware::MaxGraphAssemblies][8];
    alignas(NMHardware::Assembly) static unsigned char capacityStorage[
        sizeof(NMHardware::Assembly) * NMHardware::MaxGraphAssemblies];
    NMHardware::Assembly *capacityChildren =
        reinterpret_cast<NMHardware::Assembly *>(capacityStorage);
    for (size_t i = 0; i < NMHardware::MaxGraphAssemblies; ++i)
    {
        snprintf(capacityIds[i], sizeof(capacityIds[i]), "a%u",
                 static_cast<unsigned>(i));
        new (&capacityChildren[i]) NMHardware::Assembly(capacityIds[i]);
    }
    const NMHardware::Assembly capacityRoot(
        "capacity", nullptr, nullptr, NMHardware::AssemblyKind::Generic,
        nullptr, nullptr, nullptr, nullptr,
        {capacityChildren, NMHardware::MaxGraphAssemblies});
    const NMHardware::Profile capacityProfile{
        "capacity", nullptr, 0, &capacityRoot, 1, nullptr, 0};
    const bool capacityRejected = hasHardwareDiagnostic(
        capacityProfile, NMHardware::DiagnosticCode::CapacityExceeded);

    static NMHardware::TopologyGraph topologyGraph;
    static NMHardware::InferredNets inferredNets;
    const bool graphBuilt = NMHardware::buildTopologyGraph(profile, topologyGraph) &&
                            NMHardware::inferNets(topologyGraph, inferredNets);
    int controllerData = -1;
    int probeData = -1;
    for (size_t i = 0; i < topologyGraph.nodeCount; ++i)
    {
        if (graphNodeMatches(topologyGraph, i, "controller", "mcu", "GPIO4"))
            controllerData = static_cast<int>(i);
        if (graphNodeMatches(topologyGraph, i, "probe", "sensor", "DATA"))
            probeData = static_cast<int>(i);
    }
    const bool dataNetInferred = controllerData >= 0 && probeData >= 0 &&
        inferredNets.netByNode[controllerData] == inferredNets.netByNode[probeData];
    static NMHardware::TopologyGraph catalogGraph;
    static NMHardware::InferredNets catalogNets;
    const bool catalogGraphBuilt = NMHardware::buildTopologyGraph(catalogProfile, catalogGraph) &&
                                   NMHardware::inferNets(catalogGraph, catalogNets);
    int nestedMcu = -1;
    int nestedDisplay = -1;
    for (size_t i = 0; i < catalogGraph.nodeCount; ++i)
    {
        if (graphNodeMatches(catalogGraph, i, "mycroft/esp32", "mcu", "GPIO4"))
            nestedMcu = static_cast<int>(i);
        if (graphNodeMatches(catalogGraph, i, "mycroft", "display", "SCK"))
            nestedDisplay = static_cast<int>(i);
    }
    const bool nestedDefinitionPathsWork = catalogGraphBuilt && nestedMcu >= 0 &&
        nestedDisplay >= 0 &&
        catalogNets.netByNode[nestedMcu] == catalogNets.netByNode[nestedDisplay];
    static NMHardware::TopologyGraph illegalGraph;
    NMHardware::ValidationResult illegalGraphDiagnostics;
    const bool illegalGraphRejected =
        !NMHardware::buildTopologyGraph(illegalBoundaryProfile, illegalGraph,
                                        &illegalGraphDiagnostics) &&
        topologyGraph.edgeCount >= profile.connectionCount &&
        illegalGraph.edgeCount == topologyGraph.edgeCount - profile.connectionCount;
    const TelemetryResult hardware = Telemetry.getHardware();
    const TelemetryResult packedHardware = Telemetry.getHardwareMessagePack();
    JsonDocument decodedHardware;
    const bool hardwareDecoded = packedHardware.valid &&
        TelemetryService::decodeHardware(packedHardware.data, decodedHardware);
    String decodedHardwareJson;
    serializeJson(decodedHardware, decodedHardwareJson);
    JsonDocument badHardwareVersion;
    JsonArray badVersionRoot = badHardwareVersion.to<JsonArray>();
    badVersionRoot.add(NMHardware::HardwareEncodingVersion + 1);
    badVersionRoot.add(NMHardware::HwConfigVersion);
    badVersionRoot.add("main");
    badVersionRoot.add<JsonArray>();
    badVersionRoot.add<JsonArray>();
    badVersionRoot.add<JsonArray>();
    String badVersionPayload;
    serializeMsgPack(badHardwareVersion, badVersionPayload);
    JsonDocument malformedHardware;
    JsonArray malformedRoot = malformedHardware.to<JsonArray>();
    malformedRoot.add(NMHardware::HardwareEncodingVersion);
    malformedRoot.add(NMHardware::HwConfigVersion);
    malformedRoot.add("main");
    String malformedPayload;
    serializeMsgPack(malformedHardware, malformedPayload);
    JsonDocument rejectedHardware;
    const bool hardwareFailuresSafe =
        !TelemetryService::decodeHardware(badVersionPayload, rejectedHardware) &&
        !TelemetryService::decodeHardware(malformedPayload, rejectedHardware);
    const TelemetryResult info = Telemetry.getInfo();
    const NightMareResults hardwareCommand = handleNightMareCommand("HW JSON");
    const NightMareResults removedConnections = handleNightMareCommand("INFO HWCONNECTIONS");
    smokeState.setFlag("hardware_topology",
                        hardwareValidation.valid() && standardDefinitionsValid &&
                            boundaryRejected && canonicalConflictRejected &&
                            nullIdRejected && invalidIdRejected &&
                            unknownDefinitionRejected && cycleRejected &&
                            duplicateMemberRejected && unresolvedTerminalRejected &&
                            unresolvedContactRejected && duplicateConnectionRejected &&
                            selfConnectionRejected && capacityRejected &&
                            graphBuilt && dataNetInferred && nestedDefinitionPathsWork &&
                            illegalGraphRejected &&
                            hardware.valid && hardwareDecoded && hardwareFailuresSafe &&
                            decodedHardwareJson == hardware.data &&
                            hardware.data.indexOf("esp32-devkit:test") >= 0 &&
                            hardware.data.indexOf("\"version\":2") >= 0 &&
                            hardware.data.indexOf("\"host_assembly\":\"controller\"") >= 0 &&
                            hardware.data.indexOf("\"definitions\"") >= 0 &&
                            hardware.data.indexOf("\"kind\":\"sensor_probe\"") >= 0 &&
                            hardware.data.indexOf("\"canonical_net\":\"GND\"") >= 0 &&
                            hardware.data.indexOf("\"wire\":{\"color\":\"yellow\"}") >= 0 &&
                            hardware.data.indexOf("\"a\"") >= 0 &&
                            hardware.data.indexOf("\"connections\"") >= 0 &&
                            hardware.data.indexOf("\"nets\"") < 0 && info.valid &&
                            info.data.indexOf("hwconnections") < 0 &&
                            hardwareCommand.result && !removedConnections.result);
    // Events: transient occurrences. Never retained, never replayed, never state.
    // A clean persisted-source file keeps this independent of earlier runs.
    LittleFS.remove("/remoteresources.json");
    ResourcesManager eventManager;
    RecordingPublisher eventPublisher;
    RecordingSubscriber eventSubscriber;
    ManagedEvent<uint32_t> beep("acoustic:beep");
    RemoteEvent<uint32_t> watched("beep", NetDeviceIdentity("watson"));
    RemoteEvent<String> unsourced("unsourced_event");
    watched.onEvent = recordEvent;
    eventManager.setSubscriber(&eventSubscriber);
    eventManager.setPublisher(&eventPublisher);

    // The local listener sees the occurrence even while nothing can carry it.
    beep.onEvent = recordLocalEvent;
    const bool unboundFireRejected = !beep.fire(1) && beep.lastUpdateMs() == 0 &&
                                     localEventCalls == 1 && lastLocalEvent == 1;
    const bool eventsBound = eventManager.bindResource(&beep) &&
                             eventManager.bindResource(&watched) &&
                             eventManager.bindResource(&unsourced);
    const String beepTopic = gDeviceIdentity.getDeviceName() + "/resource/acoustic:beep/event";

    // Provider manifests: JSON names the kind and type and nothing else; the
    // compact form is [2, name, typeEnum] under the bumped encoding version.
    JsonDocument providerPacked;
    const bool providerPackedParsed =
        !deserializeMsgPack(providerPacked, eventPublisher.lastManifestPacked);
    bool compactEventShape = false;
    for (JsonArrayConst entry : providerPacked[2].as<JsonArrayConst>())
        if (entry[1].as<String>() == "acoustic:beep")
            compactEventShape = entry.size() == 3 &&
                                entry[0].as<uint8_t>() == static_cast<uint8_t>(NetResourceType::EVENT) &&
                                entry[2].as<uint8_t>() == static_cast<uint8_t>(NetValueType::INTEGER);
    JsonDocument expandedProvider;
    const bool providerExpanded =
        ResourcesManager::decodeManifest(eventPublisher.lastManifestPacked, expandedProvider) &&
        expandedProvider["resources"][0]["kind"].as<String>() == "event" &&
        expandedProvider["resources"][0]["type"].as<String>() == "integer" &&
        expandedProvider["resources"][0].size() == 3;
    const bool providerManifests =
        providerPackedParsed &&
        (NM_ENABLE_JSON_WIRE ? eventPublisher.lastManifestJson.length() != 0
                             : eventPublisher.lastManifestJson.length() == 0) &&
        providerPacked[0].as<uint8_t>() == ManifestEncodingVersion &&
        compactEventShape && providerExpanded;
    eventPublisher.failJsonDocuments = true;
    const bool optionalJsonCannotFailCanonical = eventManager.publishManifest() &&
                                                  eventManager.publishConsumeManifest();
    eventPublisher.failJsonDocuments = false;

    // Binding subscribes a Remote event to its owner's /event and manifest, and
    // a Managed one to nothing: no /set, no /invoke.
    const bool eventSubscriptions =
        eventSubscriber.subscribedTo("watson/resource/beep/event") &&
        eventSubscriber.subscribedTo("watson/manifest/msgpack") &&
        eventSubscriber.subscribed.indexOf("acoustic:beep") < 0 &&
        eventSubscriber.subscribed.indexOf("unsourced_event") < 0;

    // The same payload twice is two occurrences, published transient on /event.
    const int publishedBefore = eventPublisher.eventPublishes;
    const bool firedTwice = beep.fire(42) && beep.fire(42) &&
                            eventPublisher.eventPublishes == publishedBefore + 2 &&
                            eventPublisher.lastEventTopic == beepTopic &&
                            eventPublisher.lastEventPayload == "42" &&
                            eventPublisher.eventRetained == 0 && beep.lastUpdateMs() != 0;
    const uint32_t firedAt = beep.lastUpdateMs();
    delay(2);
    eventPublisher.failEvent = true;
    const bool refusedNotRecorded = !beep.fire(43) && beep.lastUpdateMs() == firedAt;
    eventPublisher.failEvent = false;
    // 1 unbound + 2 identical + 1 the transport refused: every valid fire() is
    // one local call, whatever happened to the publication.
    const bool localListenerSeesEveryFire = localEventCalls == 4 && lastLocalEvent == 43;
    // An invalid payload is not an occurrence: rejected and never announced locally.
    ManagedEvent<String> emptyEvent("empty_payload");
    emptyEvent.onEvent = countStringEvent;
    const bool emptyPayloadRejected = !emptyEvent.fire(String()) && stringEventCalls == 0;

    // A reconnect rebuilds subscriptions and republishes the manifest. It never
    // re-fires, and never invokes a callback.
    const int publishedBeforeReconnect = eventPublisher.eventAttempts;
    eventSubscriber.clear();
    eventManager.announceAll();
    eventManager.subscribeAll();
    const bool reconnectDoesNotReplay =
        eventPublisher.eventAttempts == publishedBeforeReconnect && eventCallbacks == 0 &&
        eventSubscriber.subscribedTo("watson/resource/beep/event") &&
        eventSubscriber.subscribed.indexOf("acoustic:beep") < 0;

    // Every valid live occurrence calls onEvent, identical payloads included.
    const bool firstConsumed = eventManager.handleIngressMessage("watson/resource/beep/event", "5");
    const bool secondConsumed = eventManager.handleIngressMessage("watson/resource/beep/event", "5");
    const bool repeatedDelivered = firstConsumed && secondConsumed && eventCallbacks == 2 &&
                                   lastEventSeen == 5 && watched.lastUpdateMs() != 0;
    const uint32_t receivedAt = watched.lastUpdateMs();
    delay(2);
    const bool malformedConsumed =
        eventManager.handleIngressMessage("watson/resource/beep/event", "not-a-number") &&
        eventManager.handleIngressMessage("watson/resource/beep/event", "");
    const bool malformedIgnored = malformedConsumed && eventCallbacks == 2 &&
                                  watched.lastUpdateMs() == receivedAt;
    // A retained /event is a broker replay from a publisher that broke the
    // protocol: consumed, never delivered.
    const bool retainedReplayIgnored =
        eventManager.handleIngressMessage("watson/resource/beep/event", "5", true) &&
        eventCallbacks == 2 && watched.lastUpdateMs() == receivedAt;
    const bool ownEchoNotDelivered =!eventManager.handleIngressMessage(beepTopic, "5");

    // Console: described and sourced, never fired, read, written or tuned.
    const ActionResult bare = eventManager.executeCommand(" beep");
    const ActionResult listing = eventManager.executeCommand("list");
    const int publishedBeforeVerbs = eventPublisher.eventAttempts;
    const bool verbsRejected =
        !eventManager.executeCommand(" beep get").success &&
        !eventManager.executeCommand(" beep set 1").success &&
        !eventManager.executeCommand(" beep invoke").success &&
        !eventManager.executeCommand(" beep advertise").success &&
        !eventManager.executeCommand(" beep poll").success &&
        !eventManager.executeCommand(" acoustic:beep fire 1").success &&
        !eventManager.executeCommand(" acoustic:beep source watson/beep").success &&
        eventPublisher.eventAttempts == publishedBeforeVerbs;
    const bool consoleDescribes =
        bare.success && bare.result.startsWith("EVENT beep last_update_ms=") &&
        listing.success && listing.result.indexOf("managed last_update_ms=") >= 0 &&
        listing.result.indexOf("remote source=watson/beep last_update_ms=") >= 0 &&
        listing.result.indexOf("remote source=- last_update_ms=0") >= 0;
    const ActionResult commandSource =
        eventManager.executeCommand(" unsourced_event source holmes/lamp");
    const bool commandSourceWorks =
        commandSource.success && unsourced.owner() == "holmes" &&
        unsourced.sourceResource() == "lamp" &&
        eventManager.executeCommand(" unsourced_event source").result == "holmes/lamp" &&
        eventSubscriber.subscribedTo("holmes/resource/lamp/event") &&
        eventManager.executeCommand(" unsourced_event source clear").success &&
        unsourced.sourceResource().length() == 0 &&
        eventSubscriber.unsubscribedFrom("holmes/resource/lamp/event");

    // Retargeting drops the old ingress, subscribes the new one, forgets the old
    // source's occurrences, persists the mapping and republishes what is consumed.
    eventSubscriber.clear();
    const bool retargeted = watched.setSource("holmes", "chime");
    JsonDocument consumedPacked;
    const bool consumePackedParsed =
        !deserializeMsgPack(consumedPacked, eventPublisher.lastConsumePacked);
    bool compactRemote = false;
    for (JsonArrayConst entry : consumedPacked[2].as<JsonArrayConst>())
        if (entry[1].as<String>() == "beep")
            compactRemote = entry.size() == 6 &&
                            entry[0].as<uint8_t>() == static_cast<uint8_t>(NetResourceType::EVENT) &&
                            entry[2].as<bool>() && entry[3].as<String>() == "holmes" &&
                            entry[4].as<String>() == "chime" &&
                            entry[5].as<uint8_t>() == static_cast<uint8_t>(NetValueType::INTEGER);
    JsonDocument expandedConsume;
    const bool consumeExpanded =
        ResourcesManager::decodeConsumeManifest(eventPublisher.lastConsumePacked,
                                                expandedConsume) &&
        expandedConsume["remotes"][0]["kind"].as<String>() == "event" &&
        expandedConsume["remotes"][0]["type"].as<String>() == "integer";
    File persistedFile = LittleFS.open("/remoteresources.json", "r");
    const String persisted = persistedFile ? persistedFile.readString() : String();
    if (persistedFile)
        persistedFile.close();
    const bool sourceRetargeted =
        retargeted && watched.lastUpdateMs() == 0 &&
        eventSubscriber.unsubscribedFrom("watson/resource/beep/event") &&
        eventSubscriber.unsubscribedFrom("watson/manifest/msgpack") &&
        eventSubscriber.subscribedTo("holmes/resource/chime/event") &&
        eventSubscriber.subscribedTo("holmes/manifest/msgpack") &&
        consumePackedParsed && consumedPacked[0].as<uint8_t>() == ConsumeManifestEncodingVersion &&
        consumedPacked.size() == 3 && compactRemote && consumeExpanded &&
        persisted.indexOf("\"beep\":\"holmes/chime\"") >= 0 &&
        !eventManager.handleIngressMessage("watson/resource/beep/event", "5") &&
        eventManager.handleIngressMessage("holmes/resource/chime/event", "6") &&
        eventCallbacks == 3 && lastEventSeen == 6 && watched.lastUpdateMs() != 0;

    // The persisted mapping comes back through the ordinary Remote source path.
    ResourcesManager reloadManager;
    RemoteEvent<uint32_t> reloaded("beep");
    const bool sourceReloaded = reloadManager.bindResource(&reloaded) &&
                                reloadManager.loadRemoteSources() &&
                                reloaded.owner() == "holmes" &&
                                reloaded.sourceResource() == "chime";

    // A manifest only describes: a mismatched payload type is reported, never a
    // reason to stop delivering.
    JsonDocument mismatchedManifest;
    JsonArray mismatchedRoot = mismatchedManifest.to<JsonArray>();
    mismatchedRoot.add(ManifestEncodingVersion);
    mismatchedRoot.add(ResourceManifestVersion);
    JsonArray mismatchedItems = mismatchedRoot.add<JsonArray>();
    JsonArray mismatchedEvent = mismatchedItems.add<JsonArray>();
    mismatchedEvent.add(static_cast<uint8_t>(NetResourceType::EVENT));
    mismatchedEvent.add("chime");
    mismatchedEvent.add(static_cast<uint8_t>(NetValueType::STRING));
    // The type enum for STRING is 0, a NUL byte that the String writer inside
    // serializeMsgPack() would cut off; the binary-safe serializer keeps it.
    String encodedMismatch;
    const bool mismatchEncoded =
        serializeWholeDocument(mismatchedManifest, DocumentFormat::MSGPACK,
                               encodedMismatch) == PayloadResult::Complete;
    const bool manifestDoesNotGate =
        mismatchEncoded &&
        eventManager.handleIngressMessage("holmes/manifest/msgpack", encodedMismatch) &&
        eventManager.handleIngressMessage("holmes/resource/chime/event", "7") &&
        eventCallbacks == 4 && lastEventSeen == 7;

    // Clearing leaves a discoverable, unbound declaration and forgets the source.
    eventSubscriber.clear();
    const bool cleared = watched.clearSource();
    File clearedFile = LittleFS.open("/remoteresources.json", "r");
    const String persistedAfterClear = clearedFile ? clearedFile.readString() : String();
    if (clearedFile)
        clearedFile.close();
    JsonDocument clearedConsume;
    const bool clearedConsumeDecoded = ResourcesManager::decodeConsumeManifest(
        eventPublisher.lastConsumePacked, clearedConsume);
    const bool sourceCleared =
        cleared && watched.lastUpdateMs() == 0 &&
        eventSubscriber.unsubscribedFrom("holmes/resource/chime/event") &&
        clearedConsumeDecoded && clearedConsume["remotes"].size() == 2 &&
        !clearedConsume["remotes"][0]["bound"].as<bool>() &&
        !clearedConsume["remotes"][1]["bound"].as<bool>() &&
        persistedAfterClear.indexOf("\"beep\"") < 0;

    // Unbinding: a Managed event just leaves the manifest, with no tombstone to
    // retract (nothing was retained); a Remote one gives its subscription and
    // persisted source back.
    const int attemptsBeforeUnbind = eventPublisher.eventAttempts;
    eventManager.unbindResource(&beep);
    JsonDocument manifestAfterUnbind;
    const bool managedUnbound =
        !beep.isBound() && ResourcesManager::decodeManifest(
                               eventPublisher.lastManifestPacked, manifestAfterUnbind) &&
        !manifestHasResource(manifestAfterUnbind, "acoustic:beep") &&
        eventPublisher.eventAttempts == attemptsBeforeUnbind;
    watched.setSource("holmes", "chime");
    eventSubscriber.clear();
    eventManager.unbindResource(&watched);
    File unboundFile = LittleFS.open("/remoteresources.json", "r");
    const String persistedAfterUnbind = unboundFile ? unboundFile.readString() : String();
    if (unboundFile)
        unboundFile.close();
    JsonDocument consumeAfterUnbind;
    const bool remoteUnbound =
        !watched.isBound() && eventSubscriber.unsubscribedFrom("holmes/resource/chime/event") &&
        persistedAfterUnbind.indexOf("\"beep\"") < 0 &&
        ResourcesManager::decodeConsumeManifest(eventPublisher.lastConsumePacked,
                                                consumeAfterUnbind) &&
        consumeAfterUnbind["remotes"].size() == 1;

    // Readers refuse positional documents in an encoding they do not speak.
    JsonDocument oldManifest;
    JsonArray oldRoot = oldManifest.to<JsonArray>();
    oldRoot.add(ManifestEncodingVersion - 1);
    oldRoot.add(ResourceManifestVersion);
    oldRoot.add<JsonArray>();
    String encodedOldManifest;
    serializeMsgPack(oldManifest, encodedOldManifest);
    JsonDocument oldConsume;
    JsonArray oldConsumeRoot = oldConsume.to<JsonArray>();
    oldConsumeRoot.add(ConsumeManifestEncodingVersion - 1);
    oldConsumeRoot.add(ConsumeManifestVersion);
    oldConsumeRoot.add<JsonArray>();
    String encodedOldConsume;
    serializeMsgPack(oldConsume, encodedOldConsume);
    JsonDocument ignored;
    const bool oldEncodingsFallBack =
        !ResourcesManager::decodeManifest(encodedOldManifest, ignored) &&
        !ResourcesManager::decodeConsumeManifest(encodedOldConsume, ignored);

    smokeState.setFlag("event_resources",
                       unboundFireRejected && eventsBound && providerManifests &&
                           optionalJsonCannotFailCanonical &&
                           eventSubscriptions && firedTwice && refusedNotRecorded &&
                           localListenerSeesEveryFire &&
                           emptyPayloadRejected && reconnectDoesNotReplay && repeatedDelivered &&
                           malformedIgnored && retainedReplayIgnored &&
                           ownEchoNotDelivered && consoleDescribes &&
                           verbsRejected && commandSourceWorks && sourceRetargeted &&
                           sourceReloaded && manifestDoesNotGate && sourceCleared &&
                           managedUnbound && remoteUnbound && oldEncodingsFallBack &&
                           eventPublisher.eventRetained == 0);

    Telemetry.start();
}

void loop() {}
