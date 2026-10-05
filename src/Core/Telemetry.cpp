#include <NightMare/Features.h>
#if NM_ENABLE_TELEMETRY

#include "Telemetry.h"
#include "DeviceIdentity.h"
#include "Scheduler.h"
#include <esp_heap_caps.h>
#include "DocumentPayload.h"
#include <NightMare/HardwareProfile.h>
#include <Network/NmConnection.h>
#include <Network/GatewayCandidate.h>
#include <Network/NmConnectionInternal.h>
#include <esp_system.h>
#if NM_ENABLE_WIFI
#include <Network/WiFiIP/NmWifiService.h>
#endif
#if NM_ENABLE_WIFI_RADIO
#include <Network/WiFiRadio/NmWifiRadioService.h>
#endif
#if NM_NETWORK_ESPNOW
#include <Network/EspNow/NmEspNowConnection.h>
#include <Network/EspNow/EspNowClient.h>
#endif
#if NM_ENABLE_MQTT
#include <Network/MQTT/NmMqttConnection.h>
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

    void addOptional(JsonArray dst, const char *value)
    {
        if (value != nullptr && value[0] != '\0')
            dst.add(value);
        else
            dst.add(nullptr);
    }

    void appendPackedEndpoint(JsonArray dst, const NMHardware::EndpointRef &endpoint)
    {
        dst.add(endpoint.assembly != nullptr ? endpoint.assembly : "");
        dst.add(static_cast<uint8_t>(endpoint.kind));
        dst.add(endpoint.owner != nullptr ? endpoint.owner : "");
        dst.add(endpoint.endpoint != nullptr ? endpoint.endpoint : "");
    }

    void appendPackedConnection(JsonArray dst, const NMHardware::Connection &connection)
    {
        appendPackedEndpoint(dst.add<JsonArray>(), connection.a);
        appendPackedEndpoint(dst.add<JsonArray>(), connection.b);
        const NMHardware::WireMetadata &wire = connection.wire;
        if ((wire.color == nullptr || wire.color[0] == '\0') &&
            (wire.gauge == nullptr || wire.gauge[0] == '\0') &&
            (wire.label == nullptr || wire.label[0] == '\0') && wire.lengthMm == 0)
        {
            dst.add(nullptr);
            return;
        }
        JsonArray item = dst.add<JsonArray>();
        addOptional(item, wire.color);
        addOptional(item, wire.gauge);
        addOptional(item, wire.label);
        item.add(wire.lengthMm);
    }

    void appendPackedMembers(JsonArray dst, const NMHardware::AssemblyMembers &members);

    void appendPackedAssembly(JsonArray dst, const NMHardware::Assembly &assembly)
    {
        dst.add(assembly.id != nullptr ? assembly.id : "");
        addOptional(dst, assembly.definition);
        addOptional(dst, assembly.name);
        dst.add(static_cast<uint8_t>(assembly.kind));
        addOptional(dst, assembly.model);
        addOptional(dst, assembly.manufacturer);
        addOptional(dst, assembly.serialNumber);
        addOptional(dst, assembly.location);
        appendPackedMembers(dst.add<JsonArray>(), assembly.members);
    }

    void appendPackedMembers(JsonArray dst, const NMHardware::AssemblyMembers &members)
    {
        JsonArray assemblies = dst.add<JsonArray>();
        for (size_t i = 0; i < members.assemblyCount; ++i)
            appendPackedAssembly(assemblies.add<JsonArray>(), members.assemblies[i]);

        JsonArray devices = dst.add<JsonArray>();
        for (size_t i = 0; i < members.deviceCount; ++i)
        {
            const NMHardware::Device &device = members.devices[i];
            JsonArray item = devices.add<JsonArray>();
            item.add(device.id != nullptr ? device.id : "");
            addOptional(item, device.name);
            addOptional(item, device.kind);
            addOptional(item, device.model);
            addOptional(item, device.manufacturer);
            JsonArray terminals = item.add<JsonArray>();
            for (size_t terminal = 0; terminal < device.terminalCount; ++terminal)
            {
                JsonArray endpoint = terminals.add<JsonArray>();
                endpoint.add(device.terminals[terminal].id != nullptr
                                 ? device.terminals[terminal].id : "");
                endpoint.add(static_cast<uint8_t>(device.terminals[terminal].canonicalNet));
                addOptional(endpoint, device.terminals[terminal].name);
            }
        }

        JsonArray connectors = dst.add<JsonArray>();
        for (size_t i = 0; i < members.connectorCount; ++i)
        {
            const NMHardware::Connector &connector = members.connectors[i];
            JsonArray item = connectors.add<JsonArray>();
            item.add(connector.id != nullptr ? connector.id : "");
            addOptional(item, connector.name);
            item.add(static_cast<uint8_t>(connector.kind));
            addOptional(item, connector.model);
            addOptional(item, connector.manufacturer);
            JsonArray contacts = item.add<JsonArray>();
            for (size_t contact = 0; contact < connector.contactCount; ++contact)
            {
                JsonArray endpoint = contacts.add<JsonArray>();
                endpoint.add(connector.contacts[contact].id != nullptr
                                 ? connector.contacts[contact].id : "");
                endpoint.add(static_cast<uint8_t>(connector.contacts[contact].canonicalNet));
                addOptional(endpoint, connector.contacts[contact].name);
            }
        }

        JsonArray connections = dst.add<JsonArray>();
        for (size_t i = 0; i < members.connectionCount; ++i)
            appendPackedConnection(connections.add<JsonArray>(), members.connections[i]);
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

    bool copyOptional(JsonObject dst, const char *key, JsonVariantConst value)
    {
        if (value.isNull())
            return true;
        if (!value.is<const char *>())
            return false;
        dst[key] = value.as<const char *>();
        return true;
    }

    bool decodePackedEndpoint(JsonArrayConst src, JsonObject dst)
    {
        if (src.size() < 4 || !src[0].is<const char *>() || !src[1].is<uint8_t>() ||
            !src[2].is<const char *>() || !src[3].is<const char *>())
            return false;
        const uint8_t kind = src[1].as<uint8_t>();
        if (kind > static_cast<uint8_t>(NMHardware::EndpointKind::ConnectorContact))
            return false;
        dst["assembly"] = src[0].as<const char *>();
        dst["kind"] = NMHardware::endpointKindName(static_cast<NMHardware::EndpointKind>(kind));
        dst["owner"] = src[2].as<const char *>();
        dst["endpoint"] = src[3].as<const char *>();
        return true;
    }

    bool decodePackedConnection(JsonArrayConst src, JsonObject dst)
    {
        if (src.size() < 3 || !src[0].is<JsonArrayConst>() || !src[1].is<JsonArrayConst>() ||
            (!src[2].isNull() && !src[2].is<JsonArrayConst>()))
            return false;
        if (!decodePackedEndpoint(src[0].as<JsonArrayConst>(), dst["a"].to<JsonObject>()) ||
            !decodePackedEndpoint(src[1].as<JsonArrayConst>(), dst["b"].to<JsonObject>()))
            return false;
        if (src[2].isNull())
            return true;
        JsonArrayConst wire = src[2].as<JsonArrayConst>();
        if (wire.size() < 4 || !wire[3].is<uint32_t>())
            return false;
        JsonObject out = dst["wire"].to<JsonObject>();
        if (!copyOptional(out, "color", wire[0]) || !copyOptional(out, "gauge", wire[1]) ||
            !copyOptional(out, "label", wire[2]))
            return false;
        if (wire[3].as<uint32_t>() != 0)
            out["length_mm"] = wire[3].as<uint32_t>();
        return true;
    }

    bool decodePackedMembers(JsonArrayConst src, JsonObject dst);

    bool decodePackedAssembly(JsonArrayConst src, JsonObject dst)
    {
        if (src.size() < 9 || !src[0].is<const char *>() || !src[3].is<uint8_t>() ||
            !src[8].is<JsonArrayConst>())
            return false;
        const uint8_t kind = src[3].as<uint8_t>();
        if (kind > static_cast<uint8_t>(NMHardware::AssemblyKind::Generic))
            return false;
        dst["id"] = src[0].as<const char *>();
        if (!copyOptional(dst, "definition", src[1]) || !copyOptional(dst, "name", src[2]) ||
            !copyOptional(dst, "model", src[4]) || !copyOptional(dst, "manufacturer", src[5]) ||
            !copyOptional(dst, "serial_number", src[6]) || !copyOptional(dst, "location", src[7]))
            return false;
        if (src[1].isNull() || kind != static_cast<uint8_t>(NMHardware::AssemblyKind::Generic))
            dst["kind"] = NMHardware::assemblyKindName(static_cast<NMHardware::AssemblyKind>(kind));
        return decodePackedMembers(src[8].as<JsonArrayConst>(), dst);
    }

    bool decodePackedMembers(JsonArrayConst src, JsonObject dst)
    {
        if (src.size() < 4 || !src[0].is<JsonArrayConst>() || !src[1].is<JsonArrayConst>() ||
            !src[2].is<JsonArrayConst>() || !src[3].is<JsonArrayConst>())
            return false;
        JsonArray assemblies = dst["assemblies"].to<JsonArray>();
        for (JsonVariantConst value : src[0].as<JsonArrayConst>())
            if (!value.is<JsonArrayConst>() ||
                !decodePackedAssembly(value.as<JsonArrayConst>(), assemblies.add<JsonObject>()))
                return false;

        JsonArray devices = dst["devices"].to<JsonArray>();
        for (JsonVariantConst value : src[1].as<JsonArrayConst>())
        {
            if (!value.is<JsonArrayConst>()) return false;
            JsonArrayConst item = value.as<JsonArrayConst>();
            if (item.size() < 6 || !item[0].is<const char *>() || !item[5].is<JsonArrayConst>())
                return false;
            JsonObject out = devices.add<JsonObject>();
            out["id"] = item[0].as<const char *>();
            if (!copyOptional(out, "name", item[1]) || !copyOptional(out, "kind", item[2]) ||
                !copyOptional(out, "model", item[3]) || !copyOptional(out, "manufacturer", item[4]))
                return false;
            JsonArray terminals = out["terminals"].to<JsonArray>();
            for (JsonVariantConst endpointValue : item[5].as<JsonArrayConst>())
            {
                if (!endpointValue.is<JsonArrayConst>()) return false;
                JsonArrayConst endpoint = endpointValue.as<JsonArrayConst>();
                if (endpoint.size() < 3 || !endpoint[0].is<const char *>() ||
                    !endpoint[1].is<uint8_t>()) return false;
                const uint8_t net = endpoint[1].as<uint8_t>();
                if (net > static_cast<uint8_t>(NMHardware::CanonicalNet::ProtectiveEarth)) return false;
                JsonObject terminal = terminals.add<JsonObject>();
                terminal["id"] = endpoint[0].as<const char *>();
                if (!copyOptional(terminal, "name", endpoint[2])) return false;
                if (net != static_cast<uint8_t>(NMHardware::CanonicalNet::None))
                    terminal["canonical_net"] = NMHardware::canonicalNetName(static_cast<NMHardware::CanonicalNet>(net));
            }
        }

        JsonArray connectors = dst["connectors"].to<JsonArray>();
        for (JsonVariantConst value : src[2].as<JsonArrayConst>())
        {
            if (!value.is<JsonArrayConst>()) return false;
            JsonArrayConst item = value.as<JsonArrayConst>();
            if (item.size() < 6 || !item[0].is<const char *>() || !item[2].is<uint8_t>() ||
                !item[5].is<JsonArrayConst>()) return false;
            const uint8_t kind = item[2].as<uint8_t>();
            if (kind > static_cast<uint8_t>(NMHardware::ConnectorKind::Generic)) return false;
            JsonObject out = connectors.add<JsonObject>();
            out["id"] = item[0].as<const char *>();
            if (!copyOptional(out, "name", item[1]) || !copyOptional(out, "model", item[3]) ||
                !copyOptional(out, "manufacturer", item[4])) return false;
            out["kind"] = NMHardware::connectorKindName(static_cast<NMHardware::ConnectorKind>(kind));
            JsonArray contacts = out["contacts"].to<JsonArray>();
            for (JsonVariantConst endpointValue : item[5].as<JsonArrayConst>())
            {
                if (!endpointValue.is<JsonArrayConst>()) return false;
                JsonArrayConst endpoint = endpointValue.as<JsonArrayConst>();
                if (endpoint.size() < 3 || !endpoint[0].is<const char *>() ||
                    !endpoint[1].is<uint8_t>()) return false;
                const uint8_t net = endpoint[1].as<uint8_t>();
                if (net > static_cast<uint8_t>(NMHardware::CanonicalNet::ProtectiveEarth)) return false;
                JsonObject contact = contacts.add<JsonObject>();
                contact["id"] = endpoint[0].as<const char *>();
                if (!copyOptional(contact, "name", endpoint[2])) return false;
                if (net != static_cast<uint8_t>(NMHardware::CanonicalNet::None))
                    contact["canonical_net"] = NMHardware::canonicalNetName(static_cast<NMHardware::CanonicalNet>(net));
            }
        }

        JsonArray connections = dst["connections"].to<JsonArray>();
        for (JsonVariantConst value : src[3].as<JsonArrayConst>())
            if (!value.is<JsonArrayConst>() ||
                !decodePackedConnection(value.as<JsonArrayConst>(), connections.add<JsonObject>()))
                return false;
        return true;
    }

    // The connection topic of each publishable document; null for query-only sections.
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

void TelemetryService::buildPositionalHardware(JsonDocument &doc) const
{
    const NMHardware::Profile profile = NMHardware::getProfile();
    JsonArray root = doc.to<JsonArray>();
    root.add(NMHardware::HardwareEncodingVersion);
    root.add(NMHardware::HwConfigVersion);
    root.add(profile.hostAssembly != nullptr ? profile.hostAssembly : "");
    JsonArray definitions = root.add<JsonArray>();
    for (size_t i = 0; i < profile.definitionCount; ++i)
    {
        const NMHardware::HardwareDefinition &definition = profile.definitions[i];
        JsonArray item = definitions.add<JsonArray>();
        item.add(definition.id != nullptr ? definition.id : "");
        item.add(static_cast<uint8_t>(definition.kind));
        addOptional(item, definition.name);
        addOptional(item, definition.model);
        addOptional(item, definition.manufacturer);
        appendPackedMembers(item.add<JsonArray>(), definition.members);
    }
    JsonArray roots = root.add<JsonArray>();
    for (size_t i = 0; i < profile.rootCount; ++i)
        appendPackedAssembly(roots.add<JsonArray>(), profile.roots[i]);
    JsonArray connections = root.add<JsonArray>();
    for (size_t i = 0; i < profile.connectionCount; ++i)
        appendPackedConnection(connections.add<JsonArray>(), profile.connections[i]);
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
    features["espnow"] = NM_NETWORK_ESPNOW != 0;
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
    JsonObject transport = dst["transport"].to<JsonObject>();
    transport["preferred"] = NightMare::ConnectionTypeName(NightMare::GetPreferredConnection());
    transport["active"] = NightMare::ConnectionTypeName(NightMare::GetActiveConnection());
    transport["state"] = NightMare::ConnectionStateName(NightMare::GetConnectionState());

    JsonObject radio = dst["wifi_radio"].to<JsonObject>();
#if NM_ENABLE_WIFI_RADIO
    radio["supported"] = NightMare::WiFiRadio_supported();
    radio["enabled"] = NightMare::WiFiRadio_enabled();
    radio["state"] = NightMare::ConnectivityStateName(NightMare::WiFiRadio_state());
#else
    radio["supported"] = false;
    radio["enabled"] = false;
    radio["state"] = "STOPPED";
#endif

    JsonObject wifiIp = dst["wifi_ip"].to<JsonObject>();
#if NM_ENABLE_WIFI
    const NightMare::WiFiInfo wifi = WiFi_info();
    const bool wifiConnected = wifi.state == NightMare::WiFiState::CONNECTED;
    wifiIp["supported"] = true;
    wifiIp["enabled"] = NightMare::WiFiIP_enabled();
    wifiIp["state"] = NightMare::ConnectivityStateName(NightMare::WiFiIP_state());
    if (wifiConnected)
    {
        wifiIp["ssid"] = wifi.ssid.c_str();
        wifiIp["ip"] = wifi.ip.c_str();
        wifiIp["rssi"] = wifi.rssi;
        wifiIp["channel"] = wifi.channel;
        wifiIp["tx_power_dbm"] = wifi.txPowerDbm;
    }
#else
    wifiIp["supported"] = false;
    wifiIp["enabled"] = false;
    wifiIp["state"] = "STOPPED";
#endif

    JsonObject espNow = dst["esp_now"].to<JsonObject>();
#if NM_NETWORK_ESPNOW
    espNow["supported"] = true;
    espNow["enabled"] = NightMare::EspNow_enabled();
    espNow["state"] = NightMare::ConnectivityStateName(NightMare::EspNow_state());
    espNow["gateway_connected"] = NightMare::EspNow_state() == NightMare::ConnectivityState::CONNECTED;
    espNow["channel"] = WiFiRadio_channel();
    espNow["rtt_ms"] = NightMare::EspNowClient::rttMs();
    espNow["session_id"] = NightMare::EspNowClient::sessionId();
#else
    espNow["supported"] = false;
    espNow["enabled"] = false;
    espNow["state"] = "STOPPED";
#endif

    JsonObject mqtt = dst["mqtt"].to<JsonObject>();
#if NM_ENABLE_MQTT
    mqtt["supported"] = true;
    mqtt["enabled"] = NightMare::Mqtt_enabled();
    mqtt["state"] = NightMare::ConnectivityStateName(NightMare::Mqtt_state());
    mqtt["profile"] = NightMare::MqttProfileName(NightMare::Mqtt_profile());
#else
    mqtt["supported"] = false;
    mqtt["enabled"] = false;
    mqtt["state"] = "STOPPED";
    mqtt["profile"] = "MQTT";
#endif

    const NightMare::GatewayCandidateStatus candidate = NightMare::GatewayCandidateGet();
    JsonObject gateway = dst["gateway_candidate"].to<JsonObject>();
    gateway["known"] = candidate.known;
    gateway["id"] = candidate.id;
#if NM_ENABLE_MQTT
    gateway["probable"] = NightMare::GatewayCandidateIsProbable(NightMare::Mqtt_profile());
#else
    gateway["probable"] = false;
#endif
    gateway["esp_now_ready"] = candidate.espNowReady;
    gateway["remote_mqtt_ready"] = candidate.remoteMqttReady;
    gateway["local_mqtt_ready"] = candidate.localMqttReady;
    gateway["ssid"] = candidate.ssid;
    gateway["bssid"] = candidate.bssid;
    gateway["channel"] = candidate.channel;
}

TelemetryResult TelemetryService::getInfo(InfoType type) const
{
    TelemetryResult result;
    if (type == InfoType::INVALID)
        return result;

    // Sizes itself as it is filled; the old fixed capacity asked for
    // 2048 + connections * 256 bytes contiguous, which on a board describing
    // eighteen pins was a 6.6KB block demanded on every connection connect.
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
    const PayloadResult outcome = serializeWholeDocument(doc, DocumentFormat::JSON, result.data);
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
                           NightMare::PublishText(gDeviceIdentity.topic(topic), info.data, type != InfoType::HEARTBEAT);
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
    const PayloadResult outcome = serializeWholeDocument(doc, DocumentFormat::JSON, result.data);
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

TelemetryResult TelemetryService::getHardwareMessagePack() const
{
    TelemetryResult result;
    const NMHardware::ValidationResult validation = NMHardware::validateHwConfig(NMHardware::getProfile());
    if (!validation.valid())
        return result;
    JsonDocument doc;
    buildPositionalHardware(doc);
    result.valid = serializeWholeDocument(doc, DocumentFormat::MSGPACK, result.data) ==
                   PayloadResult::Complete;
    return result;
}

bool TelemetryService::decodeHardware(const String &encoded, JsonDocument &into)
{
    JsonDocument packed;
    if (deserializeMsgPack(packed, encoded.c_str(), encoded.length()) || !packed.is<JsonArray>())
        return false;
    JsonArrayConst root = packed.as<JsonArrayConst>();
    if (root.size() < 6 || !root[0].is<uint8_t>() ||
        root[0].as<uint8_t>() != NMHardware::HardwareEncodingVersion ||
        !root[1].is<uint8_t>() || root[1].as<uint8_t>() != NMHardware::HwConfigVersion ||
        !root[2].is<const char *>() || !root[3].is<JsonArrayConst>() ||
        !root[4].is<JsonArrayConst>() || !root[5].is<JsonArrayConst>())
        return false;

    JsonDocument decoded;
    decoded["version"] = root[1].as<uint8_t>();
    decoded["host_assembly"] = root[2].as<const char *>();
    JsonArray definitions = decoded["definitions"].to<JsonArray>();
    for (JsonVariantConst value : root[3].as<JsonArrayConst>())
    {
        if (!value.is<JsonArrayConst>()) return false;
        JsonArrayConst item = value.as<JsonArrayConst>();
        if (item.size() < 6 || !item[0].is<const char *>() || !item[1].is<uint8_t>() ||
            !item[5].is<JsonArrayConst>()) return false;
        const uint8_t kind = item[1].as<uint8_t>();
        if (kind > static_cast<uint8_t>(NMHardware::AssemblyKind::Generic)) return false;
        JsonObject out = definitions.add<JsonObject>();
        out["id"] = item[0].as<const char *>();
        out["kind"] = NMHardware::assemblyKindName(static_cast<NMHardware::AssemblyKind>(kind));
        if (!copyOptional(out, "name", item[2]) || !copyOptional(out, "model", item[3]) ||
            !copyOptional(out, "manufacturer", item[4]) ||
            !decodePackedMembers(item[5].as<JsonArrayConst>(), out)) return false;
    }
    JsonArray roots = decoded["roots"].to<JsonArray>();
    for (JsonVariantConst value : root[4].as<JsonArrayConst>())
        if (!value.is<JsonArrayConst>() ||
            !decodePackedAssembly(value.as<JsonArrayConst>(), roots.add<JsonObject>())) return false;
    JsonArray connections = decoded["connections"].to<JsonArray>();
    for (JsonVariantConst value : root[5].as<JsonArrayConst>())
        if (!value.is<JsonArrayConst>() ||
            !decodePackedConnection(value.as<JsonArrayConst>(), connections.add<JsonObject>())) return false;
    if (decoded.overflowed())
        return false;
    into.clear();
    into.set(decoded);
    return !into.overflowed();
}

bool TelemetryService::publishHardware()
{
    const TelemetryResult hardware = getHardwareMessagePack();
    if (!hardware.valid ||
        !NightMare::Publish(resolveDocumentTopic(gDeviceIdentity.getDeviceName(), "hardware",
                                                DocumentFormat::MSGPACK).c_str(),
                            reinterpret_cast<const uint8_t *>(hardware.data.c_str()),
                            hardware.data.length(), true))
        return false;
#if NM_ENABLE_JSON_WIRE
    const TelemetryResult json = getHardware();
    if (!json.valid || !NightMare::PublishText(
                           resolveDocumentTopic(gDeviceIdentity.getDeviceName(), "hardware",
                                                DocumentFormat::JSON),
                           json.data, true))
        LOG_WARNING("TEL", "Canonical hardware published; optional JSON sibling failed");
#endif
    return true;
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
