#pragma once

#include <Arduino.h>

namespace NightMare
{
struct RGB
{
    RGB(uint8_t red = 0, uint8_t green = 0, uint8_t blue = 0)
        : r(red), g(green), b(blue) {}

    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct RGBA
{
    RGBA(uint8_t red = 0, uint8_t green = 0, uint8_t blue = 0, uint8_t alpha = 0)
        : r(red), g(green), b(blue), a(alpha) {}

    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

/// Hue, saturation, and value use the full byte range 0..255. Hue wraps from
/// 255 back to 0; saturation and value are linear byte intensities.
struct HSV
{
    HSV(uint8_t hue = 0, uint8_t saturation = 0, uint8_t value = 0)
        : h(hue), s(saturation), v(value) {}

    uint8_t h;
    uint8_t s;
    uint8_t v;
};
}

/// @brief An RGBA colour packed as 0xRRGGBBAA.
class ColourType
{
public:
    ColourType() = default;
    explicit ColourType(uint32_t value) : value_(value) {}
    ColourType(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

    uint32_t rawValue() const { return value_; }

    NightMare::RGB toRGB() const;
    NightMare::RGBA toRGBA() const;
    NightMare::HSV toHSV() const;

    static ColourType fromHSV(const NightMare::HSV &hsv);

    bool operator==(const ColourType &other) const { return value_ == other.value_; }
    bool operator!=(const ColourType &other) const { return !(*this == other); }

private:
    uint32_t value_ = 0;
};

/// Canonical unsigned decimal representation of the packed 0xRRGGBBAA value.
String encodeColour(const ColourType &colour);

/// Accepts decimal raw values, #/0x RRGGBB or RRGGBBAA, rgb(), rgba(), and hsv().
/// Returns false and leaves `colour` unchanged when input is malformed or out of range.
bool decodeColour(const String &encoded, ColourType &colour);
