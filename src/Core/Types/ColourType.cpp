#include "ColourType.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

namespace
{
uint32_t pack(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return (static_cast<uint32_t>(r) << 24) |
           (static_cast<uint32_t>(g) << 16) |
           (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a);
}

bool parseDecimal(const String &text, uint32_t maximum, uint32_t &out)
{
    String component = text;
    component.trim();
    if (component.length() == 0)
        return false;
    for (size_t i = 0; i < component.length(); ++i)
        if (!isdigit(static_cast<unsigned char>(component[i])))
            return false;

    errno = 0;
    char *end = nullptr;
    const unsigned long long parsed = strtoull(component.c_str(), &end, 10);
    if (errno == ERANGE || end == component.c_str() || *end != '\0' || parsed > maximum)
        return false;
    out = static_cast<uint32_t>(parsed);
    return true;
}

bool parseComponents(const String &text, const char *functionName, size_t expected,
                     uint8_t *values)
{
    String lower = text;
    lower.toLowerCase();
    const String prefix = String(functionName) + "(";
    if (!lower.startsWith(prefix) || !lower.endsWith(")"))
        return false;

    const String body = text.substring(prefix.length(), text.length() - 1);
    size_t start = 0;
    for (size_t i = 0; i < expected; ++i)
    {
        const int comma = body.indexOf(',', start);
        if ((i + 1 < expected && comma < 0) || (i + 1 == expected && comma >= 0))
            return false;
        const size_t end = comma < 0 ? body.length() : static_cast<size_t>(comma);
        uint32_t value = 0;
        if (!parseDecimal(body.substring(start, end), 255, value))
            return false;
        values[i] = static_cast<uint8_t>(value);
        start = end + 1;
    }
    return true;
}

int hexDigit(char value)
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}

bool parseHexColour(const String &text, ColourType &out)
{
    size_t offset = 0;
    if (text.startsWith("#"))
        offset = 1;
    else if (text.startsWith("0x") || text.startsWith("0X"))
        offset = 2;
    else
        return false;

    const size_t digits = text.length() - offset;
    if (digits != 6 && digits != 8)
        return false;
    uint32_t value = 0;
    for (size_t i = offset; i < text.length(); ++i)
    {
        const int digit = hexDigit(text[i]);
        if (digit < 0)
            return false;
        value = (value << 4) | static_cast<uint32_t>(digit);
    }
    if (digits == 6)
        value = (value << 8) | 0xffU;
    out = ColourType(value);
    return true;
}
}

ColourType::ColourType(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    : value_(pack(r, g, b, a))
{
}

NightMare::RGB ColourType::toRGB() const
{
    return {static_cast<uint8_t>(value_ >> 24), static_cast<uint8_t>(value_ >> 16),
            static_cast<uint8_t>(value_ >> 8)};
}

NightMare::RGBA ColourType::toRGBA() const
{
    return {static_cast<uint8_t>(value_ >> 24), static_cast<uint8_t>(value_ >> 16),
            static_cast<uint8_t>(value_ >> 8), static_cast<uint8_t>(value_)};
}

NightMare::HSV ColourType::toHSV() const
{
    const NightMare::RGB rgb = toRGB();
    const uint8_t maximum = max(rgb.r, max(rgb.g, rgb.b));
    const uint8_t minimum = min(rgb.r, min(rgb.g, rgb.b));
    const uint8_t delta = maximum - minimum;

    NightMare::HSV hsv{};
    hsv.v = maximum;
    hsv.s = maximum == 0 ? 0 : static_cast<uint8_t>(lroundf(delta * 255.0f / maximum));
    if (delta == 0)
        return hsv;

    float hue;
    if (maximum == rgb.r)
        hue = 60.0f * fmodf((rgb.g - rgb.b) / static_cast<float>(delta), 6.0f);
    else if (maximum == rgb.g)
        hue = 60.0f * ((rgb.b - rgb.r) / static_cast<float>(delta) + 2.0f);
    else
        hue = 60.0f * ((rgb.r - rgb.g) / static_cast<float>(delta) + 4.0f);
    if (hue < 0.0f)
        hue += 360.0f;
    hsv.h = static_cast<uint8_t>(lroundf(hue * 255.0f / 360.0f));
    return hsv;
}

ColourType ColourType::fromHSV(const NightMare::HSV &hsv)
{
    const float hue = fmodf(hsv.h * 360.0f / 255.0f, 360.0f);
    const float saturation = hsv.s / 255.0f;
    const float value = hsv.v / 255.0f;
    const float chroma = value * saturation;
    const float hueSector = hue / 60.0f;
    const float x = chroma * (1.0f - fabsf(fmodf(hueSector, 2.0f) - 1.0f));

    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    if (hueSector < 1.0f)
    {
        r = chroma;
        g = x;
    }
    else if (hueSector < 2.0f)
    {
        r = x;
        g = chroma;
    }
    else if (hueSector < 3.0f)
    {
        g = chroma;
        b = x;
    }
    else if (hueSector < 4.0f)
    {
        g = x;
        b = chroma;
    }
    else if (hueSector < 5.0f)
    {
        r = x;
        b = chroma;
    }
    else
    {
        r = chroma;
        b = x;
    }

    const float match = value - chroma;
    return ColourType(static_cast<uint8_t>(lroundf((r + match) * 255.0f)),
                      static_cast<uint8_t>(lroundf((g + match) * 255.0f)),
                      static_cast<uint8_t>(lroundf((b + match) * 255.0f)));
}

String encodeColour(const ColourType &colour)
{
    const uint32_t raw = colour.rawValue();
    char encoded[11];
    if (static_cast<uint8_t>(raw) == 0xff)
        snprintf(encoded, sizeof(encoded), "%lu", static_cast<unsigned long>(raw >> 8));
    else
        snprintf(encoded, sizeof(encoded), "#%08lX", static_cast<unsigned long>(raw));
    return String(encoded);
}

bool decodeColour(const String &encoded, ColourType &colour)
{
    String text = encoded;
    text.trim();
    if (text.length() == 0)
        return false;

    ColourType decoded;
    if (parseHexColour(text, decoded))
    {
        colour = decoded;
        return true;
    }

    uint8_t values[4] = {};
    if (parseComponents(text, "rgb", 3, values))
        decoded = ColourType(values[0], values[1], values[2]);
    else if (parseComponents(text, "rgba", 4, values))
        decoded = ColourType(values[0], values[1], values[2], values[3]);
    else if (parseComponents(text, "hsv", 3, values))
        decoded = ColourType::fromHSV(NightMare::HSV(values[0], values[1], values[2]));
    else
    {
        uint32_t raw = 0;
        if (!parseDecimal(text, 0xFFFFFF, raw))
            return false;
        decoded = ColourType((raw << 8) | 0xffU);
    }

    colour = decoded;
    return true;
}
