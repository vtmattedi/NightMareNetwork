#include "HardwareProfile.h"

#if __has_include(<NightMareHardware.h>)
#include <NightMareHardware.h>
#define NM_HAS_PROJECT_HARDWARE 1
#else
#define NM_HAS_PROJECT_HARDWARE 0
#endif

namespace NMHardware
{
void Resistor::setValue(double value)
{
    digits_ = InvalidDigits;
    exponent_ = 0;
    if (!(value > 0.0))
        return;

    int exponent = 0;
    while (value >= 10.0 && exponent < 127)
    {
        value /= 10.0;
        ++exponent;
    }
    while (value < 1.0 && exponent > -128)
    {
        value *= 10.0;
        --exponent;
    }
    int significant = static_cast<int>(value * 10.0 + 0.5);
    if (significant == 100)
    {
        significant = 10;
        ++exponent;
    }
    if (significant < 10 || significant > 99 || exponent < -128 || exponent > 127)
        return;
    digits_ = static_cast<uint8_t>(((significant / 10) << 4) | (significant % 10));
    exponent_ = static_cast<int8_t>(exponent);
}

void Resistor::setText(const char *text)
{
    digits_ = InvalidDigits;
    exponent_ = 0;
    if (text == nullptr || *text == '\0')
        return;

    String value(text);
    value.trim();
    value.toLowerCase();
    double multiplier = 1.0;
    int separator = value.indexOf('k');
    if (separator >= 0)
        multiplier = 1000.0;
    else
    {
        separator = value.indexOf('m');
        if (separator >= 0)
            multiplier = 1000000.0;
    }

    if (separator >= 0)
    {
        if (separator == static_cast<int>(value.length()) - 1)
            value.remove(separator);
        else
            value.setCharAt(separator, '.');
    }

    char *end = nullptr;
    const double parsed = strtod(value.c_str(), &end);
    if (end != value.c_str() && *end == '\0')
        setValue(parsed * multiplier);
}

double Resistor::ohms() const
{
    if (!valid())
        return 0.0;
    double value = firstDigit() + secondDigit() / 10.0;
    int exponent = exponent_;
    while (exponent > 0)
    {
        value *= 10.0;
        --exponent;
    }
    while (exponent < 0)
    {
        value /= 10.0;
        ++exponent;
    }
    return value;
}

Profile getProfile()
{
#if NM_HAS_PROJECT_HARDWARE
    return projectProfile();
#else
    return {"unspecified", nullptr, 0, nullptr, 0};
#endif
}
}
