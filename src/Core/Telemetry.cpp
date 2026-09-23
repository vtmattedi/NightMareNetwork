#include <NightMare/Features.h>
#if NM_ENABLE_TELEMETRY

#include "Telemetry.h"
#include "DeviceIdentity.h"
#include "Scheduler.h"
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

    const char *mainBoardModel(const NMHardware::Profile &profile)
    {
        if (profile.boardCount == 0 || profile.boards == nullptr ||
            profile.boards[0].model == nullptr)
            return "unspecified";
        return profile.boards[0].model;
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

    bool profileUsable(const NMHardware::Profile &profile)
    {
        if (profile.boardCount == 0 || profile.boardCount > 255 ||
            profile.deviceCount > 255 || profile.connectionCount > MaxConnections ||
            profile.boards == nullptr ||
            (profile.deviceCount != 0 && profile.devices == nullptr) ||
            (profile.connectionCount != 0 && profile.connections == nullptr))
            return false;
        for (size_t i = 0; i < profile.deviceCount; ++i)
            if (profile.devices[i].board != NMHardware::NoBoard &&
                profile.devices[i].board >= profile.boardCount)
                return false;
        for (size_t i = 0; i < profile.connectionCount; ++i)
        {
            const NMHardware::Connection &connection = profile.connections[i];
            if ((connection.device != NMHardware::NoDevice &&
                 connection.device >= profile.deviceCount) ||
                connection.signal == nullptr)
                return false;
            if (connection.resistor.valid() && connection.pull == NMHardware::Pull::None)
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
    dst["board"] = mainBoardModel(profile);
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
    }
    JsonArray connections = doc["connections"].to<JsonArray>();
    for (size_t i = 0; i < profile.connectionCount; ++i)
    {
        const NMHardware::Connection &connection = profile.connections[i];
        JsonObject pin = connections.add<JsonObject>();
        pin["pin"] = connection.pin;
        if (connection.device == NMHardware::NoDevice)
            pin["device"] = nullptr;
        else
            pin["device"] = connection.device;
        pin["signal"] = connection.signal;
        if (connection.bus == NMHardware::NoBus)
            pin["bus"] = nullptr;
        else
            pin["bus"] = connection.bus;
        pin["type"] = signalTypeName(connection.type);
        pin["direction"] = directionName(connection.direction);
        pin["pull"] = pullName(connection.pull);
        pin["active_low"] = connection.activeLow;
        if (connection.resistor.valid())
        {
            JsonArray resistor = pin["resistor"].to<JsonArray>();
            resistor.add(connection.resistor.firstDigit());
            resistor.add(connection.resistor.secondDigit());
            resistor.add(connection.resistor.exponent());
        }
    }
}

void TelemetryService::buildPositionalHardware(JsonDocument &doc) const
{
    const NMHardware::Profile profile = NMHardware::getProfile();
    JsonArray root = doc.to<JsonArray>();
    root.add(NMHardware::TopologyVersion);
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
    }
    JsonArray connections = root.add<JsonArray>();
    for (size_t i = 0; i < profile.connectionCount; ++i)
    {
        const NMHardware::Connection &connection = profile.connections[i];
        JsonArray item = connections.add<JsonArray>();
        item.add(connection.pin);
        item.add(connection.device);
        item.add(connection.signal);
        item.add(connection.bus);
        item.add(static_cast<uint8_t>(connection.type));
        item.add(static_cast<uint8_t>(connection.direction));
        item.add(static_cast<uint8_t>(connection.pull));
        item.add(connection.activeLow);
        if (connection.resistor.valid())
            item.add(connection.resistor.encoded());
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
    result.valid = serializeWholeDocument(doc, DocumentEncoding::JSON, result.data);
    if (!result.valid)
        LOG_WARNING("TEL", "Could not build the complete info document; publishing nothing");
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
    result.valid = serializeWholeDocument(
        doc, packed ? DocumentEncoding::MSGPACK : DocumentEncoding::JSON, result.data);
    if (!result.valid)
        LOG_WARNING("TEL", "Could not build the complete %s hardware document; publishing nothing",
                    packed ? "MessagePack" : "JSON");
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
