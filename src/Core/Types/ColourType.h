
struct RGB 
{
    uint8_t r;
    uint8_t g;
    uint8_t b;

};
struct RGBA 
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
};

struct HSV 
{
    uint8_t h;
    uint8_t s;
    uint8_t v;

};

class ColourType
{
private:
     uint32_t value_;
public:
    uint32_t rawValue() const;
    ColourType(uint32_t value);
    ColourType(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
    ColourType &operator=(const ColourType &other);
    ColourType fromHSV(const HSV &hsv);
    HSV toHSV() const;
    RGB toRGB() const;
    RGBA toRGBA() const;
};

//codec
// Converts a ColourType to a string representation in the format: for now: rawValue
String encodeColour(const ColourType &colour);
// decodes a string representation of a colour into a ColourType object. Returns true if successful, false otherwise.
// Supported formats: rawValue, <0x,#>RRGGBB, <0x,#>RRGGBBAA or json format: rgb(r,g,b), rgba(r,g,b,a), hsv(h,s,v)
bool decodeColour(const String &str, ColourType &colour);

