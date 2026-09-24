#include <NightMare/Features.h>
#if NM_ENABLE_TELEMETRY

#include "Telemetry.h"
#include "DeviceIdentity.h"
#include "Scheduler.h"
#include <esp_heap_caps.h>
#include "DocumentPayload.h"
#include <NightMare/HardwareProfile.h>
#include <Network/MQTT.h>
#include <esp_system.h>
#if NM_ENABLE_WIFI
#include <WiFi.h>
#endif

namespace
{
    constexpr char SystemJob[] = "nm.telemetry.system";
    constexpr char NetworkJob[] = "nm.telemetry.network";
    constexpr size_t MaxConnections = 128;

    const char *hostBoardModel(const NMHardware::Profile &profile)
    {
        if (profile.boardCount == 0 || profile.boards == nullptr ||
            profile.hostBoard >= profile.boardCount ||
            profile.boards[profile.hostBoard].model == nullptr)
            return "unspecified";
        return profile.boards[profile.hostBoard].model;
    }

    const char *directionName(NMHardware::Direction direction)
    {
        switch (direction)
        {
        case NMHardware::Direction::Input:
            return "input";
        case NMHardware::Direction::Output:
            return "output";
        case NMHardware::Direction::Bidirectional:
            return "bidirectional";
        case NMHardware::Direction::Power:
            return "power";
        case NMHardware::Direction::Ground:
            return "ground";
        case NMHardware::Direction::Bus:
            return "bus";
        }
        return "unknown";
    }

    const char *pullName(NMHardware::Pull pull)
    {
        switch (pull)
        {
        case NMHardware::Pull::None:
            return "none";
        case NMHardware::Pull::Up:
            return "up";
        case NMHardware::Pull::Down:
            return "down";
        case NMHardware::Pull::ExternalUp:
            return "external_up";
        case NMHardware::Pull::ExternalDown:
            return "external_down";
        }
        return "unknown";
    }

    const char *signalTypeName(NMHardware::SignalType type)
    {
        switch (type)
        {
        case NMHardware::SignalType::Gpio: return "gpio";
        case NMHardware::SignalType::SpiClock: return "spi_clock";
        case NMHardware::SignalType::SpiMosi: return "spi_mosi";
        case NMHardware::SignalType::SpiMiso: return "spi_miso";
        case NMHardware::SignalType::SpiChipSelect: return "spi_chip_select";
        case NMHardware::SignalType::I2cData: return "i2c_data";
        case NMHardware::SignalType::I2cClock: return "i2c_clock";
        case NMHardware::SignalType::UartTransmit: return "uart_transmit";
        case NMHardware::SignalType::UartReceive: return "uart_receive";
        case NMHardware::SignalType::Pwm: return "pwm";
        case NMHardware::SignalType::Analog: return "analog";
        case NMHardware::SignalType::OneWire: return "one_wire";
        case NMHardware::SignalType::Power: return "power";
        case NMHardware::SignalType::Ground: return "ground";
        }
        return "unknown";
    }

    const char *endpointKindName(NMHardware::EndpointKind kind)
    {
        switch (kind)
        {
        case NMHardware::EndpointKind::Board: return "board";
        case NMHardware::EndpointKind::Device: return "device";
        case NMHardware::EndpointKind::External: return "external";
        }
        return "unknown";
    }

    const char *deviceKindName(NMHardware::DeviceKind kind)
    {
        switch (kind)
        {
        case NMHardware::DeviceKind::Ic: return "ic";
        case NMHardware::DeviceKind::Led: return "led";
        case NMHardware::DeviceKind::Button: return "button";
        case NMHardware::DeviceKind::Relay: return "relay";
        case NMHardware::DeviceKind::Sensor: return "sensor";
        case NMHardware::DeviceKind::Display: return "display";
        case NMHardware::DeviceKind::Speaker: return "speaker";
        case NMHardware::DeviceKind::Buzzer: return "buzzer";
        case NMHardware::DeviceKind::Connector: return "connector";
        case NMHardware::DeviceKind::Transistor: return "transistor";
        case NMHardware::DeviceKind::Diode: return "diode";
        case NMHardware::DeviceKind::Resistor: return "resistor";
        case NMHardware::DeviceKind::Capacitor: return "capacitor";
        case NMHardware::DeviceKind::Motor: return "motor";
        case NMHardware::DeviceKind::Storage: return "storage";
        case NMHardware::DeviceKind::Unknown: break;
        }
        return "unknown";
    }

    bool endpointUsable(const NMHardware::Endpoint &endpoint,
                        const NMHardware::Profile &profile)
    {
        if (endpoint.terminal == nullptr || endpoint.terminal[0] == '\0')
            return false;
        switch (endpoint.kind)
        {
        case NMHardware::EndpointKind::Board:
            return endpoint.index < profile.boardCount;
        case NMHardware::EndpointKind::Device:
            return endpoint.index < profile.deviceCount;
        case NMHardware::EndpointKind::External:
            return true;
        }
        return false;
    }

    bool profileUsable(const NMHardware::Profile &profile)
    {
        if (profile.boardCount == 0 || profile.boardCount > 255 ||
            profile.hostBoard >= profile.boardCount || profile.deviceCount > 255 ||
            profile.netCount > 255 || profile.connectionCount > MaxConnections ||
            profile.boards == nullptr ||
            (profile.deviceCount != 0 && profile.devices == nullptr) ||
            (profile.netCount != 0 && profile.nets == nullptr) ||
            (profile.connectionCount != 0 && profile.connections == nullptr))
            return false;
        for (size_t i = 0; i < profile.deviceCount; ++i)
            if (profile.devices[i].board != NMHardware::NoBoard &&
                profile.devices[i].board >= profile.boardCount)
                return false;
        for (size_t i = 0; i < profile.netCount; ++i)
        {
            const NMHardware::Net &net = profile.nets[i];
            if (net.id == nullptr || net.id[0] == '\0' ||
                (net.resistor.valid() && net.pull == NMHardware::Pull::None))
                return false;
        }
        for (size_t i = 0; i < profile.connectionCount; ++i)
        {
            const NMHardware::Connection &connection = profile.connections[i];
            if (connection.net >= profile.netCount ||
                !endpointUsable(connection.from, profile) ||
                !endpointUsable(connection.to, profile))
                return false;
        }
        return true;
    }

    // The retained topic of each document; null for sections that have none.
    const char *documentTopic(InfoType type)
    {
        switch (type)
        {
        case InfoType::INFO:
            return "info";
        case InfoType::SYSTEM:
            return "telemetry/system";
        case InfoType::NETWORK:
            return "telemetry/network";
        default:
            return nullptr;
        }
    }
}

TelemetryService Telemetry;

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
    if (gScheduler.everyMonotonic(SystemJob, []() { Telemetry.publishInfo(InfoType::SYSTEM); },
                                  NM_TELEMETRY_INTERVAL_MS) < 0)
        return false;
    if (gScheduler.everyMonotonic(NetworkJob, []() { Telemetry.publishInfo(InfoType::NETWORK); },
                                  NM_NETWORK_TELEMETRY_INTERVAL_MS) < 0)
    {
        // Leave nothing half-installed, or a retry would trip over its own label.
        gScheduler.remove(SystemJob);
        return false;
    }
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
    dst["board"] = hostBoardModel(profile);
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
    doc["version"] = NMHardware::TopologyVersion;
    doc["host_board"] = profile.hostBoard;
    JsonArray boards = doc["boards"].to<JsonArray>();
    for (size_t i = 0; i < profile.boardCount; ++i)
    {
        JsonObject item = boards.add<JsonObject>();
        item["id"] = profile.boards[i].id != nullptr ? profile.boards[i].id : "";
        item["model"] = profile.boards[i].model != nullptr ? profile.boards[i].model : "";
    }
    JsonArray devices = doc["devices"].to<JsonArray>();
    for (size_t i = 0; i < profile.deviceCount; ++i)
    {
        JsonObject item = devices.add<JsonObject>();
        item["id"] = profile.devices[i].id != nullptr ? profile.devices[i].id : "";
        item["model"] = profile.devices[i].model != nullptr ? profile.devices[i].model : "";
        if (profile.devices[i].board == NMHardware::NoBoard)
            item["board"] = nullptr;
        else
            item["board"] = profile.devices[i].board;
        if (profile.devices[i].kind != NMHardware::DeviceKind::Unknown)
            item["kind"] = deviceKindName(profile.devices[i].kind);
        if (profile.devices[i].form != nullptr && profile.devices[i].form[0] != '\0')
            item["form"] = profile.devices[i].form;
    }
    JsonArray nets = doc["nets"].to<JsonArray>();
    for (size_t i = 0; i < profile.netCount; ++i)
    {
        const NMHardware::Net &net = profile.nets[i];
        JsonObject item = nets.add<JsonObject>();
        item["id"] = net.id != nullptr ? net.id : "";
        item["type"] = signalTypeName(net.type);
        if (net.bus == NMHardware::NoBus)
            item["bus"] = nullptr;
        else
            item["bus"] = net.bus;
        item["direction"] = directionName(net.direction);
        item["pull"] = pullName(net.pull);
        item["active_low"] = net.activeLow;
        if (net.resistor.valid())
        {
            JsonArray resistor = item["resistor"].to<JsonArray>();
            resistor.add(net.resistor.firstDigit());
            resistor.add(net.resistor.secondDigit());
            resistor.add(net.resistor.exponent());
        }
    }
    JsonArray connections = doc["connections"].to<JsonArray>();
    for (size_t i = 0; i < profile.connectionCount; ++i)
    {
        const NMHardware::Connection &connection = profile.connections[i];
        JsonObject item = connections.add<JsonObject>();
        JsonObject from = item["from"].to<JsonObject>();
        from["kind"] = endpointKindName(connection.from.kind);
        from["index"] = connection.from.index;
        from["terminal"] = connection.from.terminal;
        JsonObject to = item["to"].to<JsonObject>();
        to["kind"] = endpointKindName(connection.to.kind);
        to["index"] = connection.to.index;
        to["terminal"] = connection.to.terminal;
        item["net"] = connection.net;
        item["group"] = connection.group;
    }
}

void TelemetryService::buildPositionalHardware(JsonDocument &doc) const
{
    const NMHardware::Profile profile = NMHardware::getProfile();
    JsonArray root = doc.to<JsonArray>();
    root.add(NMHardware::TopologyVersion);
    root.add(profile.hostBoard);
    JsonArray boards = root.add<JsonArray>();
    for (size_t i = 0; i < profile.boardCount; ++i)
    {
        JsonArray item = boards.add<JsonArray>();
        item.add(profile.boards[i].id != nullptr ? profile.boards[i].id : "");
        item.add(profile.boards[i].model != nullptr ? profile.boards[i].model : "");
    }
    JsonArray devices = root.add<JsonArray>();
    for (size_t i = 0; i < profile.deviceCount; ++i)
    {
        JsonArray item = devices.add<JsonArray>();
        item.add(profile.devices[i].id != nullptr ? profile.devices[i].id : "");
        item.add(profile.devices[i].model != nullptr ? profile.devices[i].model : "");
        item.add(profile.devices[i].board);
        const bool hasForm = profile.devices[i].form != nullptr &&
                             profile.devices[i].form[0] != '\0';
        if (profile.devices[i].kind != NMHardware::DeviceKind::Unknown || hasForm)
            item.add(static_cast<uint8_t>(profile.devices[i].kind));
        if (hasForm)
            item.add(profile.devices[i].form);
    }
    JsonArray nets = root.add<JsonArray>();
    for (size_t i = 0; i < profile.netCount; ++i)
    {
        const NMHardware::Net &net = profile.nets[i];
        JsonArray item = nets.add<JsonArray>();
        item.add(net.id != nullptr ? net.id : "");
        item.add(static_cast<uint8_t>(net.type));
        item.add(net.bus);
        item.add(static_cast<uint8_t>(net.direction));
        item.add(static_cast<uint8_t>(net.pull));
        item.add(net.activeLow);
        if (net.resistor.valid())
            item.add(net.resistor.encoded());
    }
    JsonArray connections = root.add<JsonArray>();
    for (size_t i = 0; i < profile.connectionCount; ++i)
    {
        const NMHardware::Connection &connection = profile.connections[i];
        JsonArray item = connections.add<JsonArray>();
        JsonArray from = item.add<JsonArray>();
        from.add(static_cast<uint8_t>(connection.from.kind));
        from.add(connection.from.index);
        from.add(connection.from.terminal);
        JsonArray to = item.add<JsonArray>();
        to.add(static_cast<uint8_t>(connection.to.kind));
        to.add(connection.to.index);
        to.add(connection.to.terminal);
        item.add(connection.net);
        item.add(connection.group);
    }
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
    const bool wifiConnected = WiFi.status() == WL_CONNECTED;
    dst["wifi_connected"] = wifiConnected;
    if (wifiConnected)
    {
        dst["ip"] = WiFi.localIP().toString();
        dst["rssi_dbm"] = WiFi.RSSI();
    }
#endif
    dst["mqtt_connected"] = MQTT_Connected();
    dst["broker"] = MQTT_isLocal() ? "local" : "remote";
}

TelemetryResult TelemetryService::getInfo(InfoType type) const
{
    TelemetryResult result;
    if (type == InfoType::INVALID)
        return result;

    // Sizes itself as it is filled; the old fixed capacity asked for
    // 2048 + connections * 256 bytes contiguous, which on a board describing
    // eighteen pins was a 6.6KB block demanded on every MQTT connect.
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
    case InfoType::INVALID:
        return result;
    }
    // An ArduinoJson 7 document has no fixed capacity, but it can still lose a
    // value to a failed allocation, and a non-empty payload is no evidence that
    // it did not. These documents are retained, so a half-built one would be
    // read as this device's description until something replaced it.
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
    const char *topic = documentTopic(type);
    if (topic == nullptr)
        return false;
    const TelemetryResult info = getInfo(type);
    return info.valid && MQTT_Publish(topic, info.data, true, true);
}

bool TelemetryService::publishInfo(const String &type)
{
    return publishInfo(getInfoType(type));
}

TelemetryResult TelemetryService::getHardware(HardwareFormat format) const
{
    TelemetryResult result;
    const NMHardware::Profile profile = NMHardware::getProfile();
    if (!profileUsable(profile))
        return result;

    JsonDocument doc;
    if (format == HardwareFormat::MSGPACK)
        buildPositionalHardware(doc);
    else
        buildNamedHardware(doc);
    const bool packed = format == HardwareFormat::MSGPACK;
    // Same rule as the manifest, and for the same reason: a board describing
    // itself with half its pins is worse than one that has not answered yet.
    // The compact form needs this doubly -- its connections encode Direction,
    // SignalType and the device index as bare numbers, and every one of those
    // enums starts at 0, which MessagePack writes as the byte 0x00.
    const size_t measured = packed ? measureMsgPack(doc) : measureJson(doc);
    const PayloadResult outcome = serializeWholeDocument(
        doc, packed ? DocumentEncoding::MSGPACK : DocumentEncoding::JSON, result.data);
    result.valid = outcome == PayloadResult::Complete;
    if (!result.valid)
        LOG_WARNING("TEL", "Not publishing the %s hardware document (%u bytes): %s "
                          "(8bit heap free=%u largest=%u)",
                    packed ? "MessagePack" : "JSON", (unsigned)measured,
                    describePayloadResult(outcome),
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    return result;
}

bool TelemetryService::publishHardware(HardwareFormat format)
{
    const TelemetryResult hardware = getHardware(format);
    const char *topic = format == HardwareFormat::MSGPACK ? "hardware/msgpack" : "hardware";
    return hardware.valid && MQTT_Publish(topic, hardware.data, true, true);
}

bool TelemetryService::publishHardware()
{
    const bool json = publishHardware(HardwareFormat::JSON);
    const bool msgpack = publishHardware(HardwareFormat::MSGPACK);
    return json && msgpack;
}

bool TelemetryService::publishAll()
{
    // Each is attempted even if an earlier one fails.
    const bool info = publishInfo(InfoType::INFO);
    const bool hardware = publishHardware();
    const bool system = publishInfo(InfoType::SYSTEM);
    const bool network = publishInfo(InfoType::NETWORK);
    return info && hardware && system && network;
}

#endif // NM_ENABLE_TELEMETRY
