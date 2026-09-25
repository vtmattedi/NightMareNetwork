#include <NightMareNetwork.h>

#include <type_traits>
#include <utility>

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

static_assert(HasSetValue<ManagedSensor<int>>::value, "ManagedSensor must be writable locally");
static_assert(!HasSetValue<RemoteSensor<int>>::value, "RemoteSensor must not expose setValue");
static_assert(HasSetValue<ManagedState<int>>::value, "ManagedState must expose setValue");
static_assert(HasSetValue<RemoteState<int>>::value, "RemoteState must expose setValue");
static_assert(!HasFreshnessField<RemoteSensor<int>>::value, "freshness is framework-internal");
static_assert(!HasEncodedValue<ManagedSensor<int>>::value, "wire encoding is framework-internal");
static_assert(!std::is_constructible<NetValue<int>, const String &>::value,
              "NetValue is an implementation base, not an application resource");

ManagedSensor<int> managedSensor("managed_sensor");
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
};

int writeCalls = 0;
bool acceptStateWrite(ManagedState<int> &, const int &)
{
    ++writeCalls;
    return true;
}
}

void setup()
{
    PersistentSettings.begin();
    PersistentSettings.setFlag("smoke", true);
    smokeState.setFlag("smoke", PersistentSettings.getFlag("smoke"));
    managedState.onWrite = acceptStateWrite;
    const bool localStateUsesWritePolicy = managedState.setValue(7) && writeCalls == 1 &&
                                           managedState.getValue() == 7;
    managedSensor.setValue(3);
    remoteSensor.setSource("outside-node", "temperature");
    remoteState.setSource("outside-node", "target");
    remoteAction.setSource("outside-node", "remote_action");
    gResourcesManager.bindResource(&managedSensor);
    gResourcesManager.bindResource(&remoteSensor);
    gResourcesManager.bindResource(&otherTemperature);
    gResourcesManager.bindResource(&managedState);
    gResourcesManager.bindResource(&remoteState);
    gResourcesManager.bindResource(&managedAction);
    gResourcesManager.bindResource(&remoteAction);
    gResourcesManager.handleIngressMessage("outside-node/resource/temperature/state", "18");
    gResourcesManager.handleIngressMessage("inside-node/resource/temperature/state", "24");

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
    const bool resourceCommandsWork = list.success && list.result.startsWith("TYPE") &&
                                      list.result.indexOf("VALUE") >= 0 &&
                                      list.result.indexOf("outside_temperature") >= 0 &&
                                      local.success && local.result == "18" &&
                                      qualified.success && qualified.result == "18";
    smokeState.setFlag("resource_api", localStateUsesWritePolicy &&
                                            managedSensor.name() == "managed_sensor" &&
                                            managedSensor.owner().length() != 0 &&
                                            managedSensor.kind() == NetResourceType::VALUE &&
                                            managedSensor.type() == NetValueType::INTEGER &&
                                            !managedSensor.isRemote() && remoteSensor.isRemote() &&
                                            remoteSensor.hasValue() && !remoteSensor.isStale() &&
                                            resourceCommandsWork && consumeCodecWorks &&
                                            consumeLifecycleWorks);
    const String timezone = gDeviceIdentity.getTimezone();
    const NightMareResults timezoneQuery = handleNightMareCommand("TIMEZONE");
    const NightMareResults timezoneSet =
        handleNightMareCommand(String("TIMEZONE SET \"") + timezone + "\"");
    const NightMareResults invalidAdopt = handleNightMareCommand("CHANGE NAME all");
    smokeState.setFlag("identity_commands",
                        timezoneQuery.result && timezoneQuery.response.indexOf(timezone) >= 0 &&
                            timezoneSet.result && gDeviceIdentity.getTimezone() == timezone &&
                            !invalidAdopt.result);
    const NMHardware::Resistor r330(330);
    const NMHardware::Resistor r3k3("3k3");
    const NMHardware::Resistor r4k7("4.7k");
    const NMHardware::Resistor rSub("0.33");
    const NMHardware::Resistor rSubNumeric(0.33);
    const TelemetryResult hardware = Telemetry.getHardware(HardwareFormat::JSON);
    const TelemetryResult packedHardware = Telemetry.getHardware(HardwareFormat::MSGPACK);
    JsonDocument packedHardwareDoc;
    const bool hardwareCompactMetadata =
        packedHardware.valid &&
        !deserializeMsgPack(packedHardwareDoc, packedHardware.data.c_str(),
                            packedHardware.data.length()) &&
        packedHardwareDoc[3][0].as<JsonArrayConst>().size() == 3 &&
        packedHardwareDoc[3][1].as<JsonArrayConst>().size() == 5 &&
        packedHardwareDoc[3][1][3].as<uint8_t>() ==
            static_cast<uint8_t>(NMHardware::DeviceKind::Sensor) &&
        packedHardwareDoc[3][1][4].as<String>() == "waterproof-probe" &&
        packedHardwareDoc[3][2].as<JsonArrayConst>().size() == 5 &&
        packedHardwareDoc[3][2][3].as<uint8_t>() == 0 &&
        packedHardwareDoc[3][2][4].as<String>() == "panel-mount";
    const TelemetryResult info = Telemetry.getInfo();
    const NightMareResults hardwareCommand = handleNightMareCommand("HW JSON");
    const NightMareResults removedConnections = handleNightMareCommand("INFO HWCONNECTIONS");
    smokeState.setFlag("hardware_topology",
                        sizeof(NMHardware::Resistor) == 2 &&
                            r330.firstDigit() == 3 && r330.secondDigit() == 3 &&
                            r330.exponent() == 2 && r3k3.exponent() == 3 &&
                            r4k7.firstDigit() == 4 && r4k7.secondDigit() == 7 &&
                            r4k7.exponent() == 3 &&
                            rSub.exponent() == -1 && rSubNumeric.exponent() == -1 &&
                            hardware.valid &&
                            hardware.data.indexOf("esp32-devkit:test") >= 0 &&
                            hardware.data.indexOf("button-board:test") >= 0 &&
                            hardware.data.indexOf("\"host_board\":0") >= 0 &&
                            hardware.data.indexOf("\"boards\"") >= 0 &&
                            hardware.data.indexOf("\"board\":1") >= 0 &&
                            hardware.data.indexOf("\"kind\":\"sensor\"") >= 0 &&
                            hardware.data.indexOf("\"form\":\"waterproof-probe\"") >= 0 &&
                            hardware.data.indexOf("\"form\":\"panel-mount\"") >= 0 &&
                            hardware.data.indexOf("\"kind\":\"unknown\"") < 0 &&
                            hardware.data.indexOf("\"nets\"") >= 0 &&
                            hardware.data.indexOf("\"from\"") >= 0 &&
                            hardware.data.indexOf("\"group\":0") >= 0 &&
                            hardware.data.indexOf("\"connections\"") >= 0 &&
                            hardwareCompactMetadata && packedHardware.data.length() > 0 && info.valid &&
                            info.data.indexOf("hwconnections") < 0 &&
                            hardwareCommand.result && !removedConnections.result);
    Telemetry.start();
}

void loop() {}
