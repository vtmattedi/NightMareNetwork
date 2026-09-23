#include <NightMare/Features.h>
#if NM_ENABLE_TELEMETRY

#include "Telemetry.h"
#include "DeviceIdentity.h"
#include "Scheduler.h"
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
        if (profile.deviceCount > 255 || profile.connectionCount > MaxConnections ||
            (profile.deviceCount != 0 && profile.devices == nullptr) ||
            (profile.connectionCount != 0 && profile.connections == nullptr))
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
    dst["board"] = profile.boardId != nullptr ? profile.boardId : "unspecified";
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
    doc["board_id"] = profile.boardId != nullptr ? profile.boardId : "unspecified";
    JsonArray devices = doc["devices"].to<JsonArray>();
    for (size_t i = 0; i < profile.deviceCount; ++i)
    {
        JsonObject item = devices.add<JsonObject>();
        item["id"] = profile.devices[i].id != nullptr ? profile.devices[i].id : "";
        item["model"] = profile.devices[i].model != nullptr ? profile.devices[i].model : "";
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
    root.add(profile.boardId != nullptr ? profile.boardId : "unspecified");
    JsonArray devices = root.add<JsonArray>();
    for (size_t i = 0; i < profile.deviceCount; ++i)
    {
        JsonArray item = devices.add<JsonArray>();
        item.add(profile.devices[i].id != nullptr ? profile.devices[i].id : "");
        item.add(profile.devices[i].model != nullptr ? profile.devices[i].model : "");
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
    // No overflow check: an ArduinoJson 7 document has no fixed capacity.
    serializeJson(doc, result.data);
    result.valid = result.data.length() != 0;
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
    const size_t written = format == HardwareFormat::MSGPACK
                               ? serializeMsgPack(doc, result.data)
                               : serializeJson(doc, result.data);
    result.valid = written != 0;
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
