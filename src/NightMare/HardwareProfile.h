#pragma once

#include <Arduino.h>

namespace NMHardware
{
constexpr uint8_t TopologyVersion = 3;
constexpr uint8_t NoBoard = 0xff;
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

// Values are part of the MessagePack wire format. Append; never renumber.
// This is a broad semantic category, never a specific component model.
enum class DeviceKind : uint8_t
{
    Unknown = 0,
    Ic = 1,
    Led = 2,
    Button = 3,
    Relay = 4,
    Sensor = 5,
    Display = 6,
    Speaker = 7,
    Buzzer = 8,
    Connector = 9,
    Transistor = 10,
    Diode = 11,
    Resistor = 12,
    Capacitor = 13,
    Motor = 14,
    Storage = 15
};

struct Device
{
    const char *id;
    const char *model;
    // NoBoard is for a genuinely external discrete component. A standalone
    // physical PCB/module should normally be represented as its own Board.
    uint8_t board;
    DeviceKind kind;
    // Optional stable, lowercase physical-form slug. Presentation artwork
    // remains a viewer concern.
    const char *form;

    constexpr Device(const char *deviceId, const char *deviceModel,
                     uint8_t boardIndex, DeviceKind deviceKind = DeviceKind::Unknown,
                     const char *deviceForm = nullptr)
        : id(deviceId), model(deviceModel), board(boardIndex), kind(deviceKind),
          form(deviceForm) {}
};

// Values are part of the MessagePack wire format. Append; never renumber.
enum class EndpointKind : uint8_t
{
    Board = 0,
    Device = 1,
    External = 2
};

struct Endpoint
{
    EndpointKind kind;
    uint8_t index;
    const char *terminal;

    constexpr Endpoint(EndpointKind endpointKind, uint8_t endpointIndex,
                       const char *terminalName)
        : kind(endpointKind), index(endpointIndex), terminal(terminalName) {}
};

// A Net is one electrically continuous conductor. Metadata belongs here rather
// than being repeated on every physical segment of the conductor.
struct Net
{
    const char *id;
    SignalType type;
    uint8_t bus;
    Direction direction;
    Pull pull;
    bool activeLow;
    Resistor resistor;

    Net(const char *netId, SignalType signalType, uint8_t busIndex,
        Direction signalDirection, Pull pullMode = Pull::None,
        bool isActiveLow = false, Resistor pullResistor = Resistor())
        : id(netId), type(signalType), bus(busIndex), direction(signalDirection),
          pull(pullMode), activeLow(isActiveLow), resistor(pullResistor) {}
};

// A Connection is one physical segment. Segments sharing `net` are
// electrically continuous; segments sharing a non-zero `group` merely travel
// together in the same cable or harness.
struct Connection
{
    Endpoint from;
    Endpoint to;
    uint8_t net;
    uint8_t group;

    constexpr Connection(Endpoint fromEndpoint, Endpoint toEndpoint,
                         uint8_t netIndex, uint8_t physicalGroup = 0)
        : from(fromEndpoint), to(toEndpoint), net(netIndex), group(physicalGroup) {}
};

struct Profile
{
    uint8_t hostBoard;
    const Board *boards;
    size_t boardCount;
    const Device *devices;
    size_t deviceCount;
    const Net *nets;
    size_t netCount;
    const Connection *connections;
    size_t connectionCount;
};

/* MessagePack topology schema (version 3):
 *
 *   [version, hostBoard, boards[], devices[], nets[], connections[]]
 *   board      := [id, model]
 *   device     := [id, model, boardIndex, kind?, form?]
 *   net        := [id, signalType, busIndex, direction, pull, activeLow,
 *                  resistor?]
 *   connection := [fromEndpoint, toEndpoint, netIndex, group]
 *   endpoint   := [kind, index, terminal]
 *
 * `hostBoard` identifies the board running NightMare; no array position has an
 * implicit role. A device board index of 0xff remains available for genuinely
 * external discrete components, but a standalone PCB/module should be a Board.
 * Enums and array fields are append-only. The optional resistor is the two-byte
 * value returned by Resistor::encoded().
 */

// If the project has no NightMareHardware.h, this returns one "main" board
// with model "unspecified" and no advertised devices, nets or connections.
Profile getProfile();
}
