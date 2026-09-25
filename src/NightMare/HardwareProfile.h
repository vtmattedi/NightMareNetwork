#pragma once

#include <Arduino.h>

namespace NMHardware
{
constexpr uint8_t HwConfigVersion = 2;
constexpr size_t MaxDiagnostics = 24;
constexpr size_t MaxGraphEndpoints = 192;
constexpr size_t MaxGraphEdges = 192;

enum class AssemblyKind : uint8_t { CustomBoard, MarketBoard, Module, SensorProbe, Panel, Enclosure, External, Generic };
enum class ConnectorKind : uint8_t { Header, ScrewTerminal, Jst, Usb, Terminal, DirectPin, DirectWire, Generic };
enum class CanonicalNet : uint8_t { None, Gnd, Vcc, V3v3, V5v, AcPhase, AcNeutral, ProtectiveEarth };

struct Terminal
{
    const char *id;
    CanonicalNet canonicalNet = CanonicalNet::None;
    const char *name = nullptr;
    constexpr Terminal(const char *terminalId, CanonicalNet net = CanonicalNet::None,
                       const char *terminalName = nullptr)
        : id(terminalId), canonicalNet(net), name(terminalName) {}
};

struct ConnectorContact
{
    const char *id;
    CanonicalNet canonicalNet = CanonicalNet::None;
    const char *name = nullptr;
    constexpr ConnectorContact(const char *contactId,
                               CanonicalNet net = CanonicalNet::None,
                               const char *contactName = nullptr)
        : id(contactId), canonicalNet(net), name(contactName) {}
};

struct Device
{
    const char *id;
    const Terminal *terminals = nullptr;
    size_t terminalCount = 0;
    const char *name = nullptr;
    const char *kind = nullptr;
    const char *model = nullptr;
    const char *manufacturer = nullptr;
    constexpr Device(const char *deviceId, const Terminal *deviceTerminals = nullptr,
                     size_t count = 0, const char *deviceName = nullptr,
                     const char *deviceKind = nullptr, const char *deviceModel = nullptr,
                     const char *deviceManufacturer = nullptr)
        : id(deviceId), terminals(deviceTerminals), terminalCount(count),
          name(deviceName), kind(deviceKind), model(deviceModel),
          manufacturer(deviceManufacturer) {}
};

struct Connector
{
    const char *id;
    const ConnectorContact *contacts = nullptr;
    size_t contactCount = 0;
    const char *name = nullptr;
    ConnectorKind kind = ConnectorKind::Generic;
    const char *model = nullptr;
    const char *manufacturer = nullptr;
    constexpr Connector(const char *connectorId,
                        const ConnectorContact *connectorContacts = nullptr,
                        size_t count = 0, const char *connectorName = nullptr,
                        ConnectorKind connectorKind = ConnectorKind::Generic,
                        const char *connectorModel = nullptr,
                        const char *connectorManufacturer = nullptr)
        : id(connectorId), contacts(connectorContacts), contactCount(count),
          name(connectorName), kind(connectorKind), model(connectorModel),
          manufacturer(connectorManufacturer) {}
};

enum class EndpointKind : uint8_t { DeviceTerminal, ConnectorContact };

// `assembly` is slash-delimited. It is absolute in Profile::connections and
// relative to the containing assembly in definition/assembly connections.
// Empty means the containing assembly itself.
struct EndpointRef
{
    const char *assembly;
    EndpointKind kind;
    const char *owner;
    const char *endpoint;
    constexpr EndpointRef(const char *assemblyPath, EndpointKind endpointKind,
                          const char *ownerId, const char *endpointId)
        : assembly(assemblyPath), kind(endpointKind), owner(ownerId), endpoint(endpointId) {}
};

struct WireMetadata
{
    const char *color = nullptr;
    const char *gauge = nullptr;
    const char *label = nullptr;
    uint32_t lengthMm = 0;
    constexpr WireMetadata(const char *wireColor = nullptr,
                           const char *wireGauge = nullptr,
                           const char *wireLabel = nullptr,
                           uint32_t wireLengthMm = 0)
        : color(wireColor), gauge(wireGauge), label(wireLabel),
          lengthMm(wireLengthMm) {}
};

struct Connection
{
    EndpointRef a;
    EndpointRef b;
    WireMetadata wire;
    constexpr Connection(EndpointRef endpointA, EndpointRef endpointB,
                         WireMetadata wireMetadata = WireMetadata())
        : a(endpointA), b(endpointB), wire(wireMetadata) {}
};

struct Assembly;

struct AssemblyMembers
{
    const Assembly *assemblies = nullptr;
    size_t assemblyCount = 0;
    const Device *devices = nullptr;
    size_t deviceCount = 0;
    const Connector *connectors = nullptr;
    size_t connectorCount = 0;
    const Connection *connections = nullptr;
    size_t connectionCount = 0;
    constexpr AssemblyMembers(const Assembly *childAssemblies = nullptr,
                              size_t children = 0,
                              const Device *memberDevices = nullptr,
                              size_t devices = 0,
                              const Connector *memberConnectors = nullptr,
                              size_t connectors = 0,
                              const Connection *memberConnections = nullptr,
                              size_t connections = 0)
        : assemblies(childAssemblies), assemblyCount(children),
          devices(memberDevices), deviceCount(devices),
          connectors(memberConnectors), connectorCount(connectors),
          connections(memberConnections), connectionCount(connections) {}
};

struct Assembly
{
    const char *id;
    const char *definition = nullptr;
    const char *name = nullptr;
    AssemblyKind kind = AssemblyKind::Generic;
    const char *model = nullptr;
    const char *manufacturer = nullptr;
    const char *serialNumber = nullptr;
    const char *location = nullptr;
    AssemblyMembers members;
    constexpr Assembly(const char *assemblyId, const char *definitionId = nullptr,
                       const char *assemblyName = nullptr,
                       AssemblyKind assemblyKind = AssemblyKind::Generic,
                       const char *assemblyModel = nullptr,
                       const char *assemblyManufacturer = nullptr,
                       const char *assemblySerialNumber = nullptr,
                       const char *assemblyLocation = nullptr,
                       AssemblyMembers assemblyMembers = AssemblyMembers())
        : id(assemblyId), definition(definitionId), name(assemblyName),
          kind(assemblyKind), model(assemblyModel), manufacturer(assemblyManufacturer),
          serialNumber(assemblySerialNumber), location(assemblyLocation),
          members(assemblyMembers) {}
};

struct HardwareDefinition
{
    const char *id;
    AssemblyKind kind;
    const char *name = nullptr;
    const char *model = nullptr;
    const char *manufacturer = nullptr;
    AssemblyMembers members;
    constexpr HardwareDefinition(const char *definitionId,
                                 AssemblyKind assemblyKind,
                                 const char *definitionName = nullptr,
                                 const char *definitionModel = nullptr,
                                 const char *definitionManufacturer = nullptr,
                                 AssemblyMembers definitionMembers = AssemblyMembers())
        : id(definitionId), kind(assemblyKind), name(definitionName),
          model(definitionModel), manufacturer(definitionManufacturer),
          members(definitionMembers) {}
};

struct Profile
{
    const char *hostAssembly;
    const HardwareDefinition *definitions;
    size_t definitionCount;
    const Assembly *roots;
    size_t rootCount;
    const Connection *connections;
    size_t connectionCount;
    constexpr Profile(const char *host, const HardwareDefinition *profileDefinitions,
                      size_t definitions, const Assembly *profileRoots,
                      size_t rootsCount, const Connection *profileConnections,
                      size_t connectionsCount)
        : hostAssembly(host), definitions(profileDefinitions),
          definitionCount(definitions), roots(profileRoots), rootCount(rootsCount),
          connections(profileConnections), connectionCount(connectionsCount) {}
};

enum class DiagnosticCode : uint8_t
{
    InvalidProfile, InvalidId, DuplicateId, UnknownDefinition, DefinitionCycle,
    DuplicateMember, AssemblyNotFound, DeviceNotFound, ConnectorNotFound,
    TerminalNotFound, ContactNotFound, CrossAssemblyDeviceConnection,
    DuplicateConnection, CanonicalNetConflict, CapacityExceeded, InvalidValue
};

struct Diagnostic
{
    DiagnosticCode code;
    String path;
    String message;
};

struct ValidationResult
{
    Diagnostic diagnostics[MaxDiagnostics];
    size_t diagnosticCount = 0;
    bool truncated = false;
    bool valid() const { return diagnosticCount == 0 && !truncated; }
};

struct GraphNode
{
    String assembly;
    EndpointKind kind;
    const char *owner;
    const char *endpoint;
    CanonicalNet canonicalNet;
};

struct GraphEdge { uint16_t a; uint16_t b; };

struct TopologyGraph
{
    GraphNode nodes[MaxGraphEndpoints];
    size_t nodeCount = 0;
    GraphEdge edges[MaxGraphEdges];
    size_t edgeCount = 0;
};

struct InferredNets
{
    uint16_t netByNode[MaxGraphEndpoints]{};
    CanonicalNet canonicalByNet[MaxGraphEndpoints]{};
    bool conflictByNet[MaxGraphEndpoints]{};
    size_t netCount = 0;
};

const char *assemblyKindName(AssemblyKind kind);
const char *connectorKindName(ConnectorKind kind);
const char *endpointKindName(EndpointKind kind);
const char *canonicalNetName(CanonicalNet net);
const char *diagnosticCodeName(DiagnosticCode code);

ValidationResult validateHwConfig(const Profile &config);
bool buildTopologyGraph(const Profile &config, TopologyGraph &graph,
                        ValidationResult *diagnostics = nullptr);
bool inferNets(const TopologyGraph &graph, InferredNets &nets);
const char *assemblyModel(const Profile &config, const char *absolutePath);

// Projects provide this in NightMareHardware.h. Without one, the library
// returns a minimal generic root assembly named `main`.
Profile getProfile();
}
