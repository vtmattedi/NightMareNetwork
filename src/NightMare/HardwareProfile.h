#pragma once

#include <Arduino.h>

namespace NMHardware
{
constexpr uint8_t TopologyVersion = 2;
constexpr uint8_t NoBoard = 0xff;
constexpr uint8_t NoDevice = 0xff;
constexpr uint8_t NoBus = 0xff;

// Values are part of the MessagePack wire format. Append; never renumber.
enum class Direction : uint8_t
{
    Input = 0,
    Output = 1,
    Bidirectional = 2,
    Power = 3,
    Ground = 4,
    Bus = 5
};

enum class Pull : uint8_t
{
    None = 0,
    Up = 1,
    Down = 2,
    ExternalUp = 3,
    ExternalDown = 4
};

enum class SignalType : uint8_t
{
    Gpio = 0,
    SpiClock = 1,
    SpiMosi = 2,
    SpiMiso = 3,
    SpiChipSelect = 4,
    I2cData = 5,
    I2cClock = 6,
    UartTransmit = 7,
    UartReceive = 8,
    Pwm = 9,
    Analog = 10,
    OneWire = 11,
    Power = 12,
    Ground = 13
};

// Exactly two bytes: one BCD byte contains the significant digits `a` and `b`,
// and one signed byte contains `c` in a.b x 10^c. For example, 330 ohms is
// (3,3,2), 3k3 is (3,3,3), and 0.33 ohms is (3,3,-1).
class Resistor
{
public:
    Resistor() = default;
    explicit Resistor(double ohms) { setValue(ohms); }
    explicit Resistor(const char *value) { setText(value); }
    explicit Resistor(const String &value) { setText(value.c_str()); }

    bool valid() const { return digits_ != InvalidDigits; }
    uint8_t firstDigit() const { return valid() ? (digits_ >> 4) & 0x0f : 0; }
    uint8_t secondDigit() const { return valid() ? digits_ & 0x0f : 0; }
    uint8_t digits() const { return digits_; }
    int8_t exponent() const { return valid() ? exponent_ : 0; }
    uint16_t encoded() const
    {
        return static_cast<uint16_t>(digits_) << 8 |
               static_cast<uint8_t>(exponent_);
    }
    double ohms() const;

private:
    static constexpr uint8_t InvalidDigits = 0xff;
    void setValue(double ohms);
    void setText(const char *value);

    uint8_t digits_ = InvalidDigits;
    int8_t exponent_ = 0;
};
static_assert(sizeof(Resistor) == 2, "Resistor wire value must remain exactly two bytes");

struct Board
{
    const char *id;
    const char *model;
};

struct Device
{
    const char *id;
    const char *model;
    // NoBoard means a standalone component connected to the topology rather
    // than one physically integrated into a board or module.
    uint8_t board;
};

// MessagePack connection positions are kept in this same order. `resistor` is
// an optional trailing field and is emitted only for an external pull.
struct Connection
{
    int16_t pin;
    uint8_t device;
    const char *signal;
    uint8_t bus;
    SignalType type;
    Direction direction;
    Pull pull;
    bool activeLow;
    Resistor resistor;

    Connection(int16_t pinNumber, uint8_t deviceIndex, const char *signalName,
               uint8_t busIndex, SignalType signalType, Direction signalDirection,
               Pull pullMode = Pull::None, bool isActiveLow = false,
               Resistor pullResistor = Resistor())
        : pin(pinNumber), device(deviceIndex), signal(signalName), bus(busIndex),
          type(signalType), direction(signalDirection), pull(pullMode),
          activeLow(isActiveLow), resistor(pullResistor) {}
};

struct Profile
{
    const Board *boards;
    size_t boardCount;
    const Device *devices;
    size_t deviceCount;
    const Connection *connections;
    size_t connectionCount;
};

/* MessagePack topology schema (version 2):
 *
 *   [version, boards[], devices[], connections[]]
 *   board      := [id, model]
 *   device     := [id, model, boardIndex]
 *   connection := [pin, deviceIndex, signal, busIndex, signalType,
 *                  direction, pull, activeLow, resistor?]
 *
 * boards[0] is the main board. A device board index of 0xff means the component
 * is standalone. Connection device and bus indices also use 0xff for "none".
 * Enums and array fields are append-only. The optional resistor is the two-byte
 * value returned by Resistor::encoded(). Rendering coordinates, artwork and
 * icons never belong in this profile.
 */

// If the project has no NightMareHardware.h, this returns one "main" board
// with model "unspecified" and no advertised devices or connections.
Profile getProfile();
}
