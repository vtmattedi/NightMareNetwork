#include <NightMare/Features.h>
#if NM_ENABLE_TELEMETRY

#include "Telemetry.h"
#include "DeviceIdentity.h"
#include "Scheduler.h"
#include <esp_heap_caps.h>
#include "DocumentPayload.h"
#include <NightMare/HardwareProfile.h>
#include <Network/NmTransport.h>
#include <Network/NmTransportInternal.h>
#include <esp_system.h>
#if NM_ENABLE_WIFI
#include <Network/WiFi/NmWifiEsp.h>
#endif

namespace
{
    constexpr char SystemJob[] = "nm.telemetry.system";
    constexpr char NetworkJob[] = "nm.telemetry.network";
    constexpr char HeartbeatJob[] = "nm.telemetry.heartbeat";
    bool telemetrySchedulingStarted = false;

    bool validHeartbeatPeriod(int seconds)
    {
        return seconds >= HeartbeatMinPeriodSeconds &&
               seconds <= HeartbeatMaxPeriodSeconds;
    }

    void publishHeartbeat()
    {
        if (HeartbeatEnabled.value())
            Telemetry.publishInfo(InfoType::HEARTBEAT);
    }

    bool installHeartbeatJob(int seconds)
    {
        return validHeartbeatPeriod(seconds) &&
               gScheduler.everyMonotonic(HeartbeatJob, publishHeartbeat,
                                         static_cast<uint32_t>(seconds) * 1000UL) >= 0;
    }

    bool changeHeartbeatEnabled(Config<bool> &config, const bool &requested)
    {
        if (!telemetrySchedulingStarted || requested == config.value())
            return true;
        if (requested)
            return installHeartbeatJob(HeartbeatPeriod.value());
        return gScheduler.remove(HeartbeatJob);
    }

    bool changeHeartbeatPeriod(Config<int> &config, const int &requested)
    {
        if (!validHeartbeatPeriod(requested))
            return false;
        if (!telemetrySchedulingStarted || !HeartbeatEnabled.value() ||
            requested == config.value())
            return true;

        if (!gScheduler.remove(HeartbeatJob))
            return false;
        if (installHeartbeatJob(requested))
            return true;

        installHeartbeatJob(config.value());
        return false;
    }

    void optional(JsonObject object, const char *key, const char *value)
    {
        if (value != nullptr && value[0] != '\0')
            object[key] = value;
    }

    void appendEndpoint(JsonObject dst, const NMHardware::EndpointRef &endpoint)
    {
        dst["assembly"] = endpoint.assembly != nullptr ? endpoint.assembly : "";
        dst["kind"] = NMHardware::endpointKindName(endpoint.kind);
        dst["owner"] = endpoint.owner != nullptr ? endpoint.owner : "";
        dst["endpoint"] = endpoint.endpoint != nullptr ? endpoint.endpoint : "";
    }

    void appendConnection(JsonObject dst, const NMHardware::Connection &connection)
    {
        appendEndpoint(dst["a"].to<JsonObject>(), connection.a);
        appendEndpoint(dst["b"].to<JsonObject>(), connection.b);
        const NMHardware::WireMetadata &wire = connection.wire;
        if ((wire.color != nullptr && wire.color[0] != '\0') ||
            (wire.gauge != nullptr && wire.gauge[0] != '\0') ||
            (wire.label != nullptr && wire.label[0] != '\0') || wire.lengthMm != 0)
        {
            JsonObject item = dst["wire"].to<JsonObject>();
            optional(item, "color", wire.color);
            optional(item, "gauge", wire.gauge);
            optional(item, "label", wire.label);
            if (wire.lengthMm != 0)
                item["length_mm"] = wire.lengthMm;
        }
    }

    void appendMembers(JsonObject dst, const NMHardware::AssemblyMembers &members);

    void appendAssembly(JsonObject dst, const NMHardware::Assembly &assembly)
    {
        dst["id"] = assembly.id != nullptr ? assembly.id : "";
        optional(dst, "definition", assembly.definition);
        optional(dst, "name", assembly.name);
        if (assembly.definition == nullptr || assembly.definition[0] == '\0' ||
            assembly.kind != NMHardware::AssemblyKind::Generic)
            dst["kind"] = NMHardware::assemblyKindName(assembly.kind);
        optional(dst, "model", assembly.model);
        optional(dst, "manufacturer", assembly.manufacturer);
        optional(dst, "serial_number", assembly.serialNumber);
        optional(dst, "location", assembly.location);
        appendMembers(dst, assembly.members);
    }

    void appendMembers(JsonObject dst, const NMHardware::AssemblyMembers &members)
    {
        JsonArray assemblies = dst["assemblies"].to<JsonArray>();
        for (size_t i = 0; i < members.assemblyCount; ++i)
            appendAssembly(assemblies.add<JsonObject>(), members.assemblies[i]);
        JsonArray devices = dst["devices"].to<JsonArray>();
        for (size_t i = 0; i < members.deviceCount; ++i)
        {
            const NMHardware::Device &device = members.devices[i];
            JsonObject item = devices.add<JsonObject>();
            item["id"] = device.id != nullptr ? device.id : "";
            optional(item, "name", device.name);
            optional(item, "kind", device.kind);
            optional(item, "model", device.model);
            optional(item, "manufacturer", device.manufacturer);
            JsonArray terminals = item["terminals"].to<JsonArray>();
            for (size_t terminal = 0; terminal < device.terminalCount; ++terminal)
            {
                JsonObject endpoint = terminals.add<JsonObject>();
                endpoint["id"] = device.terminals[terminal].id;
                optional(endpoint, "name", device.terminals[terminal].name);
                if (device.terminals[terminal].canonicalNet != NMHardware::CanonicalNet::None)
                    endpoint["canonical_net"] = NMHardware::canonicalNetName(device.terminals[terminal].canonicalNet);
            }
        }
        JsonArray connectors = dst["connectors"].to<JsonArray>();
        for (size_t i = 0; i < members.connectorCount; ++i)
        {
            const NMHardware::Connector &connector = members.connectors[i];
            JsonObject item = connectors.add<JsonObject>();
            item["id"] = connector.id != nullptr ? connector.id : "";
            optional(item, "name", connector.name);
            item["kind"] = NMHardware::connectorKindName(connector.kind);
            optional(item, "model", connector.model);
            optional(item, "manufacturer", connector.manufacturer);
            JsonArray contacts = item["contacts"].to<JsonArray>();
            for (size_t contact = 0; contact < connector.contactCount; ++contact)
            {
                JsonObject endpoint = contacts.add<JsonObject>();
                endpoint["id"] = connector.contacts[contact].id;
                optional(endpoint, "name", connector.contacts[contact].name);
                if (connector.contacts[contact].canonicalNet != NMHardware::CanonicalNet::None)
                    endpoint["canonical_net"] = NMHardware::canonicalNetName(connector.contacts[contact].canonicalNet);
            }
        }
        JsonArray connections = dst["connections"].to<JsonArray>();
        for (size_t i = 0; i < members.connectionCount; ++i)
            appendConnection(connections.add<JsonObject>(), members.connections[i]);
    }

    // The transport topic of each publishable document; null for query-only sections.
    const char *documentTopic(InfoType type)
    {
        switch (type)
        {
        case InfoType::INFO:
            return "info";
        case InfoType::SYSTEM:
            return "telemetry/system";
        case InfoType::HEARTBEAT:
            return "telemetry/heartbeat";
        case InfoType::NETWORK:
            return "telemetry/network";
        default:
            return nullptr;
        }
    }

    const char *transportName(NightMare::TransportType transport)
    {
        switch (transport)
        {
        case NightMare::TransportType::AUTO: return "auto";
        case NightMare::TransportType::MQTT: return "mqtt";
        case NightMare::TransportType::LOCAL_MQTT: return "local_mqtt";
        case NightMare::TransportType::ESP_NOW: return "esp_now";
        }
        return "unknown";
    }
}

TelemetryService Telemetry;
Config<bool> HeartbeatEnabled("heartbeat:enable", true);
Config<int> HeartbeatPeriod("heartbeat:period", HeartbeatMinPeriodSeconds);

namespace
{
struct HeartbeatConfigHandlerInstaller
{
    HeartbeatConfigHandlerInstaller()
    {
        HeartbeatEnabled.onWrite = changeHeartbeatEnabled;
        HeartbeatPeriod.onWrite = changeHeartbeatPeriod;
    }
};

HeartbeatConfigHandlerInstaller heartbeatConfigHandlerInstaller;
}

InfoType getInfoType(const String &type)
{
    if (type.length() == 0)
        return InfoType::INFO;
    struct Entry
    {
        const char *name;
        InfoType type;
    };
    static const Entry entries[] = {
        {"INFO", InfoType::INFO},
        {"IDENTITY", InfoType::IDENTITY},
        {"HARDWARE", InfoType::HARDWARE},
        {"BUILD", InfoType::BUILD},
        {"BOOT", InfoType::BOOT},
        {"SYSTEM", InfoType::SYSTEM},
        {"HEARTBEAT", InfoType::HEARTBEAT},
        {"NETWORK", InfoType::NETWORK},
    };
    for (const Entry &entry : entries)
        if (type.equalsIgnoreCase(entry.name))
            return entry.type;
    return InfoType::INVALID;
}

bool TelemetryService::start()
{
    if (started_)
        return true;
    if (gScheduler.everyMonotonic(SystemJob, []()
                                  { Telemetry.publishInfo(InfoType::SYSTEM); }, NM_TELEMETRY_INTERVAL_MS) < 0)
        return false;
    if (gScheduler.everyMonotonic(NetworkJob, []()
                                  { Telemetry.publishInfo(InfoType::NETWORK); }, NM_NETWORK_TELEMETRY_INTERVAL_MS) < 0)
    {
        // Leave nothing half-installed, or a retry would trip over its own label.
        gScheduler.remove(SystemJob);
        return false;
    }
    if (HeartbeatEnabled.value() && !installHeartbeatJob(HeartbeatPeriod.value()))
    {
        gScheduler.remove(NetworkJob);
        gScheduler.remove(SystemJob);
        return false;
    }
    telemetrySchedulingStarted = true;
    started_ = true;
    return true;
}

void TelemetryService::appendIdentity(JsonObject dst) const
{
    dst["name"] = gDeviceIdentity.getDeviceName();
    dst["hardware"] = gDeviceIdentity.getHardwareSignature();
    dst["id"] = gDeviceIdentity.getDeviceId();
    dst["timezone"] = gDeviceIdentity.getTimezone();
}

void TelemetryService::appendHardware(JsonObject dst) const
{
    const NMHardware::Profile profile = NMHardware::getProfile();
    dst["board"] = NMHardware::assemblyModel(profile, profile.hostAssembly);
    dst["chip"] = ESP.getChipModel();
    dst["cores"] = ESP.getChipCores();
    dst["revision"] = ESP.getChipRevision();
    dst["flash_bytes"] = ESP.getFlashChipSize();
    dst["heap_bytes"] = ESP.getHeapSize();
    dst["psram_bytes"] = ESP.getPsramSize();
}

void TelemetryService::buildNamedHardware(JsonDocument &doc) const
{
    const NMHardware::Profile profile = NMHardware::getProfile();
    doc["version"] = NMHardware::HwConfigVersion;
    doc["host_assembly"] = profile.hostAssembly != nullptr ? profile.hostAssembly : "";
    JsonArray definitions = doc["definitions"].to<JsonArray>();
    for (size_t i = 0; i < profile.definitionCount; ++i)
    {
        const NMHardware::HardwareDefinition &definition = profile.definitions[i];
        JsonObject item = definitions.add<JsonObject>();
        item["id"] = definition.id != nullptr ? definition.id : "";
        item["kind"] = NMHardware::assemblyKindName(definition.kind);
        optional(item, "name", definition.name);
        optional(item, "model", definition.model);
        optional(item, "manufacturer", definition.manufacturer);
        appendMembers(item, definition.members);
    }
    JsonArray roots = doc["roots"].to<JsonArray>();
    for (size_t i = 0; i < profile.rootCount; ++i)
        appendAssembly(roots.add<JsonObject>(), profile.roots[i]);
    JsonArray connections = doc["connections"].to<JsonArray>();
    for (size_t i = 0; i < profile.connectionCount; ++i)
        appendConnection(connections.add<JsonObject>(), profile.connections[i]);
}

void TelemetryService::appendBuild(JsonObject dst) const
{
    dst["firmware_version"] = NM_FIRMWARE_VERSION;
    dst["date"] = __DATE__;
    dst["time"] = __TIME__;
    dst["compiler"] = __VERSION__;
    dst["arduino_version"] = ARDUINO;
    dst["esp_idf_version"] = ESP.getSdkVersion();
    JsonObject features = dst["features"].to<JsonObject>();
    features["settings"] = NM_ENABLE_SETTINGS != 0;
    features["network"] = NM_ENABLE_NETWORK != 0;
    features["wifi"] = NM_ENABLE_WIFI != 0;
    features["mqtt"] = NM_ENABLE_MQTT != 0;
    features["console"] = NM_ENABLE_CONSOLE != 0;
    features["resources"] = NM_ENABLE_RESOURCES != 0;
    features["scheduler"] = NM_ENABLE_SCHEDULER != 0;
    features["jobs"] = NM_ENABLE_JOBS != 0;
    features["telemetry"] = NM_ENABLE_TELEMETRY != 0;
    features["time_sync"] = NM_ENABLE_TIME_SYNC != 0;
    features["ota"] = NM_ENABLE_OTA != 0;
    features["http"] = NM_ENABLE_HTTP != 0;
    features["websocket"] = NM_ENABLE_WEBSOCKET != 0;
    features["lvgl"] = NM_ENABLE_LVGL != 0;
}

// Only facts that never need refreshing during the boot. Uptime, time sync and
// the like change, so they belong to /telemetry/system, not here.
void TelemetryService::appendBoot(JsonObject dst) const
{
    dst["reset_reason"] = static_cast<int>(esp_reset_reason());
}

void TelemetryService::appendSystem(JsonObject dst) const
{
    dst["uptime_ms"] = millis();
    dst["cpu_mhz"] = ESP.getCpuFreqMHz();
    dst["free_heap_bytes"] = ESP.getFreeHeap();
    dst["min_free_heap_bytes"] = ESP.getMinFreeHeap();
    dst["free_psram_bytes"] = ESP.getFreePsram();
}

// Last-known bookkeeping once the device is gone; /status owns presence.
void TelemetryService::appendNetwork(JsonObject dst) const
{
#if NM_ENABLE_WIFI
    const bool wifiConnected = WiFi_isConnected();
    dst["wifi_connected"] = wifiConnected;
    if (wifiConnected)
    {
        dst["ip"] = WiFi_localIP();
        dst["rssi_dbm"] = WiFi_RSSI();
        dst["tx_power_dbm"] = WiFi_getTxPowerDbm();
    }
#endif
    const NightMare::TransportType transport = NightMare::GetSelectedTransport();
    const NightMare::TransportState state = NightMare::GetTransportState();
    dst["transport"] = transportName(transport);
    dst["transport_connected"] = state == NightMare::TransportState::CONNECTED;
    dst["transport_state"] = static_cast<uint8_t>(state);
    if (transport == NightMare::TransportType::LOCAL_MQTT ||
        transport == NightMare::TransportType::MQTT)
    {
        dst["mqtt_connected"] = state == NightMare::TransportState::CONNECTED;
        dst["broker"] = transport == NightMare::TransportType::LOCAL_MQTT
                            ? "local"
                            : "remote";
    }
}

TelemetryResult TelemetryService::getInfo(InfoType type) const
{
    TelemetryResult result;
    if (type == InfoType::INVALID)
        return result;

    // Sizes itself as it is filled; the old fixed capacity asked for
    // 2048 + connections * 256 bytes contiguous, which on a board describing
    // eighteen pins was a 6.6KB block demanded on every transport connect.
    JsonDocument doc;
    switch (type)
    {
    case InfoType::INFO:
        appendIdentity(doc["identity"].to<JsonObject>());
        appendHardware(doc["hardware"].to<JsonObject>());
        appendBuild(doc["build"].to<JsonObject>());
        appendBoot(doc["boot"].to<JsonObject>());
        break;
    case InfoType::IDENTITY:
        appendIdentity(doc.to<JsonObject>());
        break;
    case InfoType::HARDWARE:
        appendHardware(doc.to<JsonObject>());
        break;
    case InfoType::BUILD:
        appendBuild(doc.to<JsonObject>());
        break;
    case InfoType::BOOT:
        appendBoot(doc.to<JsonObject>());
        break;
    case InfoType::SYSTEM:
        appendSystem(doc.to<JsonObject>());
        break;
    case InfoType::NETWORK:
        appendNetwork(doc.to<JsonObject>());
        break;
    case InfoType::HEARTBEAT:
        doc["uptime_ms"] = millis();
        doc["heartbeat"] = heartbeatCounter_ + 1;
        break;
    case InfoType::INVALID:
        return result;
    }
    // An ArduinoJson 7 document has no fixed capacity, but it can still lose a
    // value to a failed allocation, and a non-empty payload is no evidence that
    // it did not. A partial document must never be published, retained or not.
    const PayloadResult outcome = serializeWholeDocument(doc, DocumentEncoding::JSON, result.data);
    result.valid = outcome == PayloadResult::Complete;
    if (!result.valid)
        LOG_WARNING("TEL", "Not publishing the info document: %s (8bit heap free=%u largest=%u)",
                    describePayloadResult(outcome),
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return result;
}

TelemetryResult TelemetryService::getInfo(const String &type) const
{
    return getInfo(getInfoType(type));
}

bool TelemetryService::publishInfo(InfoType type)
{
    if (type == InfoType::HEARTBEAT && !HeartbeatEnabled.value())
        return false;
    const char *topic = documentTopic(type);
    if (topic == nullptr)
        return false;
    const TelemetryResult info = getInfo(type);
    const bool published = info.valid &&
                           NightMare::PublishDeviceText(
                               topic, info.data, type != InfoType::HEARTBEAT);
    if (published && type == InfoType::HEARTBEAT)
        ++heartbeatCounter_;
    return published;
}

bool TelemetryService::publishInfo(const String &type)
{
    return publishInfo(getInfoType(type));
}

TelemetryResult TelemetryService::getHardware() const
{
    TelemetryResult result;
    const NMHardware::Profile profile = NMHardware::getProfile();
    const NMHardware::ValidationResult validation = NMHardware::validateHwConfig(profile);
    if (!validation.valid())
    {
        for (size_t i = 0; i < validation.diagnosticCount; ++i)
            LOG_WARNING("TEL", "Invalid hardware config %s at %s: %s",
                        NMHardware::diagnosticCodeName(validation.diagnostics[i].code),
                        validation.diagnostics[i].path.c_str(),
                        validation.diagnostics[i].message.c_str());
        return result;
    }

    JsonDocument doc;
    buildNamedHardware(doc);
    const size_t measured = measureJson(doc);
    const PayloadResult outcome = serializeWholeDocument(doc, DocumentEncoding::JSON, result.data);
    result.valid = outcome == PayloadResult::Complete;
    if (!result.valid)
        LOG_WARNING("TEL", "Not publishing the hardware document (%u bytes): %s "
                           "(8bit heap free=%u largest=%u)",
                    (unsigned)measured,
                    describePayloadResult(outcome),
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return result;
}

bool TelemetryService::publishHardware()
{
    const TelemetryResult hardware = getHardware();
    return hardware.valid && NightMare::PublishDeviceText(
                                 "hardware", hardware.data, true);
}

bool TelemetryService::publishAll()
{
    // Each is attempted even if an earlier one fails.
    const bool info = publishInfo(InfoType::INFO);
    const bool hardware = publishHardware();
    const bool system = publishInfo(InfoType::SYSTEM);
    const bool network = publishInfo(InfoType::NETWORK);
    const bool heartbeat = !HeartbeatEnabled.value() || publishInfo(InfoType::HEARTBEAT);
    return info && hardware && system && network && heartbeat;
}

#endif // NM_ENABLE_TELEMETRY
