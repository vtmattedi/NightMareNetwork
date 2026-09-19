#pragma once
#include <NightMare/Network/CommandParser.h>
#include <NightMare/Resources/ResourceManager.h>
#include <NightMare/Storage/SettingsStore.h>

namespace NightMare {

class DeviceIdentity;
class Network;

enum class CommandSource : uint8_t { Serial, Mqtt, Application };
struct CommandContext {
    CommandSource source = CommandSource::Application;
    SettingsAccess access = SettingsAccess::User;
};
struct CommandResult {
    bool ok = false;
    String text;
};

class CommandRouter {
public:
    using Handler = CommandResult (*)(void*, const ParsedCommand&, CommandContext);
    explicit CommandRouter(ResourceManager& resources) : _resources(resources) {}
    void attachSettings(SettingsStore& settings) { _settings = &settings; }
    void attachIdentity(DeviceIdentity& identity) { _identity = &identity; }
    void attachNetwork(Network& network) { _network = &network; }
    void setFirmwareVersion(const char* version) { _firmwareVersion = version ? version : ""; }
    bool registerCommand(const char* name, Handler handler, void* context = nullptr);
    CommandResult execute(const String& line, CommandContext context = {});
    void tick(uint32_t nowMs = millis());
private:
    CommandResult invoke(const ParsedCommand& command);
    CommandResult inspect(const ParsedCommand& command);
    CommandResult operatorCommand(const ParsedCommand& command, CommandContext context);
    ResourceManager& _resources;
    SettingsStore* _settings = nullptr;
    DeviceIdentity* _identity = nullptr;
    Network* _network = nullptr;
    String _firmwareVersion;
    struct Registered { const char* name = nullptr; Handler handler = nullptr; void* context = nullptr; };
    Registered _handlers[8];
    uint32_t _restartAtMs = 0;
};

} // namespace NightMare
