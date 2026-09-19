#pragma once
#include <Arduino.h>

namespace NightMare {

class Transport {
public:
    virtual ~Transport() = default;
    virtual bool connected() const = 0;
    virtual bool publish(const String& topic, const String& payload, bool retained = false) = 0;
};

} // namespace NightMare
