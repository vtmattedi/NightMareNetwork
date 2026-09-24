#pragma once
#include <NightMare/Features.h>

#if NM_ENABLE_TELEMETRY
#include <Arduino.h>
#include <ArduinoJson.h>

// The sections that can be asked for. Hardware topology has its own retained
// documents and HW command, so it is deliberately not an INFO section.
enum class InfoType : uint8_t
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

/// @brief Maps a section name to its type, ignoring case: INFO, IDENTITY,
/// HARDWARE, BUILD, BOOT, SYSTEM, NETWORK. Empty means INFO;
/// anything else is INVALID.
InfoType getInfoType(const String &type);

struct TelemetryResult
{
    bool valid = false;
    String data;
};

enum class HardwareFormat : uint8_t
{
    JSON,
    MSGPACK
};

// Device-wide information, split by how often it changes. Five retained documents:
//   <device>/info               identity, hardware, build, boot: fixed per boot
//   <device>/hardware           hardware topology as retained JSON
//   <device>/hardware/msgpack   the same topology as retained MessagePack
//   <device>/telemetry/system   runtime health, every NM_TELEMETRY_INTERVAL_MS
//   <device>/telemetry/network  network bookkeeping, every NM_NETWORK_TELEMETRY_INTERVAL_MS
// Static INFO and hardware documents are requested on every MQTT connection
// and published cooperatively. Sensors, actuators and application state belong
// to NetResources, which carry their own freshness; nothing here duplicates them.
class TelemetryService
{
public:
    /// @brief Installs the periodic system and network publications as
    /// Scheduler callbacks. All or nothing, so it can simply be retried.
    bool start();

    /// @brief Any section, as JSON. valid is false for INVALID or on overflow.
    TelemetryResult getInfo(InfoType type = InfoType::INFO) const;
    TelemetryResult getInfo(const String &type) const;

    /// @brief Publishes one retained document: INFO, SYSTEM or NETWORK. A
    /// subsection of INFO has no topic of its own, so it returns false.
    bool publishInfo(InfoType type = InfoType::INFO);
    bool publishInfo(const String &type);

    /// @brief Builds the hardware-only topology document in readable JSON or
    /// compact positional MessagePack form.
    TelemetryResult getHardware(HardwareFormat format = HardwareFormat::JSON) const;

    /// @brief Publishes one retained hardware topology encoding.
    bool publishHardware(HardwareFormat format);

    /// @brief Publishes both retained hardware topology encodings.
    bool publishHardware();

    /// @brief /info, both /hardware encodings, and both telemetry documents.
    /// True only if all five went out.
    bool publishAll();

private:
    // Each section is written straight into its destination, so the aggregate
    // and the individual queries share one implementation and nothing is ever
    // serialized only to be parsed back.
    void appendIdentity(JsonObject dst) const;
    void appendHardware(JsonObject dst) const;
    void appendBuild(JsonObject dst) const;
    void appendBoot(JsonObject dst) const;
    void appendSystem(JsonObject dst) const;
    void appendNetwork(JsonObject dst) const;
    void buildNamedHardware(JsonDocument &doc) const;
    void buildPositionalHardware(JsonDocument &doc) const;

    bool started_ = false;
};

extern TelemetryService Telemetry;
#endif // NM_ENABLE_TELEMETRY
