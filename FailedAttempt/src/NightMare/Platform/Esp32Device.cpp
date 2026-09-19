#include <NightMare/Platform/Esp32Device.h>
#include <new>

namespace NightMare {

Esp32Device::Esp32Device()
#if NIGHTMARE_ENABLE_SETTINGS
    : _settings("config", StorageMode::Persistent, &_persistence)
#else
    : _settings("config", StorageMode::Memory)
#endif
{}

Esp32Device::~Esp32Device() {
    _runtime.stopManaged();
#if NIGHTMARE_ENABLE_MQTT
    _mqtt.end();
#endif
#if NIGHTMARE_ENABLE_CONSOLE
    if (_console) _console->~Console();
#endif
    if (_network) _network->~Network();
}

bool Esp32Device::begin(const Esp32DeviceOptions& options) {
    if (_started || !_settings.begin() || !_runtimeSettings.begin()) return false;
    String suffix = String(ESP.getEfuseMac(), HEX);
    if (!_identity.begin(_settings, suffix)) return false;
    _wifiSsid = _settings.getString("wifi_ssid", options.wifiSsid ? options.wifiSsid : "");
    _wifiPassword = _settings.getString("wifi_password", options.wifiPassword ? options.wifiPassword : "");
    _mqttUri = _settings.getString("mqtt_uri", options.mqttUri ? options.mqttUri : "");
    _mqttBackupUri = _settings.getString("mqtt_backup_uri",
        options.mqttBackupUri ? options.mqttBackupUri : "");
    _mqttUser = _settings.getString("mqtt_user", options.mqttUser ? options.mqttUser : "");
    _mqttPassword = _settings.getString("mqtt_password", options.mqttPassword ? options.mqttPassword : "");
    _mqttCertificate = options.mqttCertificate ? options.mqttCertificate : "";
    _otaPassword = options.otaPassword ? options.otaPassword : "";
    _otaEnabled = options.enableOta;
    Transport& transport =
#if NIGHTMARE_ENABLE_MQTT
        _mqtt;
#else
        _offline;
#endif
    _network = new (_networkSpace) Network(_identity.id().c_str(), transport, options.nameSpace);
#if NIGHTMARE_ENABLE_MQTT
    if (!_mqtt.attach(*_network)) return false;
    if (_mqttBackupUri.length() && !_mqtt.configureFailover(_mqttUri.c_str(),
            _mqttBackupUri.c_str(), options.brokerSwitchMs)) return false;
#endif
#if NIGHTMARE_ENABLE_CONSOLE
    _console = new (_consoleSpace) Console(_network->resources());
#if NIGHTMARE_ENABLE_SETTINGS
    _console->router().attachSettings(_settings);
#endif
    _console->router().attachIdentity(_identity);
    _console->router().attachNetwork(*_network);
    _console->router().setFirmwareVersion(options.firmwareVersion);
    _network->attachConsole(*_console);
    _serialConsole = options.serialConsole;
    if (_serialConsole && !_runtime.add(pollConsole, this)) return false;
#endif
    if (!_runtime.add(pollNetwork, this) || !_runtime.add(pollConnectivity, this)) return false;
#if NIGHTMARE_ENABLE_OTA
    if (_otaEnabled && !_runtime.add(pollOta, this)) return false;
#endif
#if NIGHTMARE_ENABLE_TELEMETRY
    if (!_telemetry.begin(_network->resources(), _runtime, options.firmwareVersion)) return false;
#endif
#if NIGHTMARE_ENABLE_WIFI
    if (_wifiSsid.length() && !_wifi.begin(_wifiSsid.c_str(), _wifiPassword.c_str(),
                                          _identity.id().c_str())) return false;
#endif
    _started = true;
    return true;
}

void Esp32Device::tick() { if (_started) _runtime.tick(); }
void Esp32Device::pollNetwork(void* context) {
    static_cast<Esp32Device*>(context)->_network->tick();
}
void Esp32Device::pollConnectivity(void* context) {
    static_cast<Esp32Device*>(context)->connectivityTick();
}
#if NIGHTMARE_ENABLE_CONSOLE
void Esp32Device::pollConsole(void* context) {
    auto* device = static_cast<Esp32Device*>(context);
    if (device->_serialConsole) device->_console->tick(*device->_serialConsole);
}
#endif
#if NIGHTMARE_ENABLE_OTA
void Esp32Device::pollOta(void* context) {
    static_cast<Esp32Device*>(context)->_ota.tick();
}
#endif

void Esp32Device::connectivityTick() {
#if NIGHTMARE_ENABLE_WIFI
    _wifi.tick();
    if (!_wifi.connected()) return;
#endif
#if NIGHTMARE_ENABLE_OTA
    if (_otaEnabled && !_otaStarted)
        _otaStarted = _ota.begin(_identity.id().c_str(),
            _otaPassword.length() ? _otaPassword.c_str() : nullptr);
#endif
#if NIGHTMARE_ENABLE_MQTT
    uint32_t now = millis();
    _mqtt.tick(now);
    if (_mqttUri.length() && !_mqttStarted &&
        (!_lastMqttAttemptMs || static_cast<uint32_t>(now - _lastMqttAttemptMs) >= 10000)) {
        _lastMqttAttemptMs = now;
        _mqttStarted = _mqtt.begin(_mqttUri.c_str(),
            _mqttUser.length() ? _mqttUser.c_str() : nullptr,
            _mqttPassword.length() ? _mqttPassword.c_str() : nullptr,
            _mqttCertificate.length() ? _mqttCertificate.c_str() : nullptr);
    }
#endif
}

} // namespace NightMare
