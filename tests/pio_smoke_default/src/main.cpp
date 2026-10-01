#include <NightMareNetwork.h>
#include <Network/NmMessageRouter.h>

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
        if (topic.endsWith("/manifest") || topic.endsWith("/manifest/msgpack"))
        {
            ++manifestAttempts;
            if (failManifest)
                return false;
            ++manifestPublishes;
            if (topic.endsWith("/manifest/msgpack"))
                lastManifestPacked = payload;
            else
                lastManifestJson = payload;
        }
        if (topic.endsWith("/manifest/consume"))
        {
            ++consumeJsonPublishes;
            lastConsumeJson = payload;
            consumeWasRetained = retained;
        }
        else if (topic.endsWith("/manifest/consume/msgpack"))
            ++consumePackedPublishes;
        return true;
    }

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
    String lastManifestJson;
    String lastManifestPacked;
    String lastStateTopic;
    String lastStatePayload;
};

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
        decodedConsume["consumes"][0]["device"].as<String>() == "outside-node" &&
        resolveResourceConsumeManifestTopic("smoke") == "smoke/manifest/consume";

    ResourcesManager consumeManager;
    RecordingPublisher consumePublisher;
    RemoteSensor<float> consumed("temperature", NetDeviceIdentity("weather-node"));
    consumeManager.setPublisher(&consumePublisher);
    const bool consumeBound = consumeManager.bindResource(&consumed);
    const bool boundPublished = consumePublisher.lastConsumeJson.indexOf("weather-node") >= 0 &&
                                consumePublisher.lastConsumeJson.indexOf("temperature") >= 0;
    consumed.setSource("relay-node", "target");
    const bool retargetPublished = consumePublisher.lastConsumeJson.indexOf("relay-node") >= 0 &&
                                   consumePublisher.lastConsumeJson.indexOf("weather-node") < 0;
    consumeManager.unbindResource(&consumed);
    const bool unbindPublished = consumePublisher.lastConsumeJson ==
                                 "{\"version\":1,\"consumes\":[]}";
    const bool consumeLifecycleWorks = consumeBound && boundPublished && retargetPublished &&
                                       unbindPublished && consumePublisher.consumeWasRetained &&
                                       consumePublisher.consumeJsonPublishes >= 4 &&
                                       consumePublisher.consumePackedPublishes >= 4;

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
                            hardware.valid &&
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
    Telemetry.start();
}

void loop() {}
