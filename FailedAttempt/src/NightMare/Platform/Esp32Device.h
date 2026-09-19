#pragma once
#include <NightMare/Features.h>
#include <NightMare/Core/DeviceIdentity.h>
#include <NightMare/Network/Network.h>
#include <NightMare/Runtime/Runtime.h>
#include <NightMare/Storage/SettingsStore.h>
#if NIGHTMARE_ENABLE_SETTINGS
#include <NightMare/Platform/Esp32SettingsPersistence.h>
#endif
#if NIGHTMARE_ENABLE_MQTT
#include <NightMare/Network/MqttTransport.h>
#endif
#if NIGHTMARE_ENABLE_WIFI
#include <NightMare/Platform/WifiStation.h>
#endif
#if NIGHTMARE_ENABLE_CONSOLE
#include <NightMare/Network/Console.h>
#endif
#if NIGHTMARE_ENABLE_TELEMETRY
#include <NightMare/Services/TelemetryService.h>
#endif
#if NIGHTMARE_ENABLE_OTA
#include <NightMare/Platform/OtaService.h>
#endif

namespace NightMare {

struct Esp32DeviceOptions {
    const char* nameSpace = "default";
    const char* wifiSsid = nullptr;
    const char* wifiPassword = nullptr;
    const char* mqttUri = nullptr;
    const char* mqttBackupUri = nullptr;
    uint32_t brokerSwitchMs = 30000;
    const char* mqttUser = nullptr;
    const char* mqttPassword = nullptr;
    const char* mqttCertificate = nullptr;
    const char* firmwareVersion = "";
    const char* otaPassword = nullptr;
    Stream* serialConsole = nullptr;
    bool enableOta = false;
};

// Owns the common stack without constructing Network before persisted identity
// is loaded. Call tick() from loop or register the device with a managed task.
class Esp32Device {
public:
    Esp32Device();
    ~Esp32Device();
    bool begin(const Esp32DeviceOptions& options = {});
    void tick();
    SettingsStore& settings() { return _settings; }
    SettingsStore& runtimeSettings() { return _runtimeSettings; }
    DeviceIdentity& identity() { return _identity; }
    Runtime& runtime() { return _runtime; }
    Network& network() { return *_network; }
    ResourceManager& resources() { return _network->resources(); }
#if NIGHTMARE_ENABLE_CONSOLE
    Console& console() { return *_console; }
#endif
#if NIGHTMARE_ENABLE_TELEMETRY
    TelemetryService& telemetry() { return _telemetry; }
#endif
#if NIGHTMARE_ENABLE_MQTT
    MqttTransport& mqtt() { return _mqtt; }
#endif
private:
    class OfflineTransport final : public Transport {
    public:
        bool connected() const override { return false; }
        bool publish(const String&, const String&, bool = false) override { return false; }
    } _offline;
    static void pollNetwork(void* context);
    static void pollConnectivity(void* context);
#if NIGHTMARE_ENABLE_CONSOLE
    static void pollConsole(void* context);
#endif
#if NIGHTMARE_ENABLE_OTA
    static void pollOta(void* context);
#endif
    void connectivityTick();
#if NIGHTMARE_ENABLE_SETTINGS
    Esp32SettingsPersistence _persistence;
#endif
    SettingsStore _settings;
    SettingsStore _runtimeSettings{"runtime", StorageMode::Memory};
    DeviceIdentity _identity;
    Runtime _runtime;
#if NIGHTMARE_ENABLE_WIFI
    WifiStation _wifi;
#endif
#if NIGHTMARE_ENABLE_MQTT
    MqttTransport _mqtt;
#endif
#if NIGHTMARE_ENABLE_OTA
    OtaService _ota;
#endif
#if NIGHTMARE_ENABLE_TELEMETRY
    TelemetryService _telemetry;
#endif
    alignas(Network) unsigned char _networkSpace[sizeof(Network)];
    Network* _network = nullptr;
#if NIGHTMARE_ENABLE_CONSOLE
    alignas(Console) unsigned char _consoleSpace[sizeof(Console)];
    Console* _console = nullptr;
    Stream* _serialConsole = nullptr;
#endif
    String _wifiSsid, _wifiPassword, _mqttUri, _mqttBackupUri, _mqttUser, _mqttPassword, _mqttCertificate;
    String _otaPassword;
    bool _started = false;
    bool _mqttStarted = false;
    bool _otaStarted = false;
    bool _otaEnabled = false;
    uint32_t _lastMqttAttemptMs = 0;
};

} // namespace NightMare
