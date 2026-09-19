#pragma once
#include <NightMare/Resources/ResourceManager.h>
#include <Stream.h>

namespace NightMare {

// Human text is only an ingress format. The same registered Action handler
// receives both this input and MQTT ACTION_INVOKE messages.
class Console {
public:
    explicit Console(ResourceManager& manager) : _manager(manager) {}
    bool execute(const String& line);
    void tick(Stream& input);
private:
    ResourceManager& _manager;
    String _line;
    bool _discarding = false;
};

} // namespace NightMare
