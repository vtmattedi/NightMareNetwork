#pragma once
#include <NightMare/Resources/ResourceManager.h>

namespace NightMare {

class Dispatcher {
public:
    explicit Dispatcher(ResourceManager& manager) : _manager(manager) {}
    bool dispatch(const String& topic, const String& payload);
private:
    ResourceManager& _manager;
};

} // namespace NightMare
