#pragma once
#include <NightMare/Features.h>

#if NM_ENABLE_TELEMETRY
#include <Arduino.h>
#include <ArduinoJson.h>

// The sections that can be asked for. Hardware configuration has its own retained
// document and HW command, so it is deliberately not an INFO section.
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

// Device-wide information, split by how often it changes. Four retained documents:
//   <device>/info               identity, hardware, build, boot: fixed per boot
//   <device>/hardware           hardware configuration as retained JSON
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

    /// @brief Builds the validated hardware configuration JSON document.
    TelemetryResult getHardware() const;

    /// @brief Publishes the retained hardware configuration document.
    bool publishHardware();

    /// @brief /info, /hardware, and both telemetry documents.
    /// True only if all four went out.
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

    bool started_ = false;
};

extern TelemetryService Telemetry;
#endif // NM_ENABLE_TELEMETRY
