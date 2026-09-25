#include "HardwareProfile.h"
#include <cstring>
#include <new>

#if __has_include(<NightMareHardware.h>)
#include <NightMareHardware.h>
#define NM_HAS_PROJECT_HARDWARE 1
#else
#define NM_HAS_PROJECT_HARDWARE 0
#endif

namespace NMHardware
{
namespace
{
bool present(const char *value) { return value != nullptr && value[0] != '\0'; }

bool validId(const char *id)
{
    if (!present(id)) return false;
    for (const char *cursor = id; *cursor != '\0'; ++cursor)
    {
        const char c = *cursor;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
            return false;
    }
    return true;
}

bool validPath(const char *path, bool emptyAllowed)
{
    if (!present(path)) return emptyAllowed;
    String remaining(path);
    while (remaining.length() != 0)
    {
        const int slash = remaining.indexOf('/');
        const String part = slash < 0 ? remaining : remaining.substring(0, slash);
        if (!validId(part.c_str())) return false;
        if (slash < 0) return true;
        remaining.remove(0, slash + 1);
    }
    return false;
}

String joinedPath(const String &base, const char *relative)
{
    if (!present(relative)) return base;
    if (base.length() == 0) return String(relative);
    return base + "/" + relative;
}

const HardwareDefinition *findDefinition(const Profile &config, const char *id)
{
    if (!present(id) || config.definitions == nullptr) return nullptr;
    for (size_t i = 0; i < config.definitionCount; ++i)
        if (present(config.definitions[i].id) && !strcmp(config.definitions[i].id, id))
            return &config.definitions[i];
    return nullptr;
}

struct AssemblyView
{
    const Assembly *instance = nullptr;
    const HardwareDefinition *definition = nullptr;
};

bool arraysPresent(const AssemblyMembers &members)
{
    return (members.assemblyCount == 0 || members.assemblies != nullptr) &&
           (members.deviceCount == 0 || members.devices != nullptr) &&
           (members.connectorCount == 0 || members.connectors != nullptr) &&
           (members.connectionCount == 0 || members.connections != nullptr);
}

size_t childCount(const AssemblyView &view)
{
    return view.instance->members.assemblyCount +
           (view.definition != nullptr ? view.definition->members.assemblyCount : 0);
}

AssemblyView childAt(const Profile &config, const AssemblyView &parent, size_t index)
{
    const size_t defined = parent.definition != nullptr
                               ? parent.definition->members.assemblyCount : 0;
    const Assembly *child = index < defined
                                ? &parent.definition->members.assemblies[index]
                                : &parent.instance->members.assemblies[index - defined];
    return {child, findDefinition(config, child->definition)};
}

AssemblyView findAssembly(const Profile &config, const char *path)
{
    AssemblyView current;
    if (!present(path) || config.roots == nullptr) return current;
    String remaining(path);
    int slash = remaining.indexOf('/');
    const String rootId = slash < 0 ? remaining : remaining.substring(0, slash);
    for (size_t i = 0; i < config.rootCount; ++i)
        if (rootId == config.roots[i].id)
        {
            current = {&config.roots[i], findDefinition(config, config.roots[i].definition)};
            break;
        }
    if (current.instance == nullptr) return current;
    while (slash >= 0)
    {
        remaining.remove(0, slash + 1);
        slash = remaining.indexOf('/');
        const String id = slash < 0 ? remaining : remaining.substring(0, slash);
        AssemblyView next;
        for (size_t i = 0; i < childCount(current); ++i)
        {
            const AssemblyView child = childAt(config, current, i);
            if (id == child.instance->id) { next = child; break; }
        }
        if (next.instance == nullptr) return next;
        current = next;
    }
    return current;
}

const Device *findDevice(const AssemblyView &view, const char *id)
{
    if (!present(id)) return nullptr;
    if (view.definition != nullptr && view.definition->members.devices != nullptr)
        for (size_t i = 0; i < view.definition->members.deviceCount; ++i)
            if (present(view.definition->members.devices[i].id) &&
                !strcmp(view.definition->members.devices[i].id, id))
                return &view.definition->members.devices[i];
    if (view.instance->members.devices != nullptr)
        for (size_t i = 0; i < view.instance->members.deviceCount; ++i)
            if (present(view.instance->members.devices[i].id) &&
                !strcmp(view.instance->members.devices[i].id, id))
                return &view.instance->members.devices[i];
    return nullptr;
}

const Connector *findConnector(const AssemblyView &view, const char *id)
{
    if (!present(id)) return nullptr;
    if (view.definition != nullptr && view.definition->members.connectors != nullptr)
        for (size_t i = 0; i < view.definition->members.connectorCount; ++i)
            if (present(view.definition->members.connectors[i].id) &&
                !strcmp(view.definition->members.connectors[i].id, id))
                return &view.definition->members.connectors[i];
    if (view.instance->members.connectors != nullptr)
        for (size_t i = 0; i < view.instance->members.connectorCount; ++i)
            if (present(view.instance->members.connectors[i].id) &&
                !strcmp(view.instance->members.connectors[i].id, id))
                return &view.instance->members.connectors[i];
    return nullptr;
}

void addDiagnostic(ValidationResult &result, DiagnosticCode code,
                   const String &path, const String &message)
{
    for (size_t i = 0; i < result.diagnosticCount; ++i)
        if (result.diagnostics[i].code == code &&
            result.diagnostics[i].path == path &&
            result.diagnostics[i].message == message)
            return;
    if (result.diagnosticCount >= MaxDiagnostics) { result.truncated = true; return; }
    result.diagnostics[result.diagnosticCount++] = {code, path, message};
}

template <typename T>
void validateIds(const T *items, size_t count, const String &path, ValidationResult &result)
{
    if (count != 0 && items == nullptr)
    {
        addDiagnostic(result, DiagnosticCode::InvalidProfile, path, "Non-zero count has a null array.");
        return;
    }
    for (size_t i = 0; i < count; ++i)
    {
        if (!validId(items[i].id))
            addDiagnostic(result, DiagnosticCode::InvalidId,
                          path + "[" + String(i) + "].id",
                          "ID is empty or contains a reserved character.");
        for (size_t previous = 0; previous < i; ++previous)
            if (present(items[i].id) && present(items[previous].id) &&
                !strcmp(items[i].id, items[previous].id))
                addDiagnostic(result, DiagnosticCode::DuplicateId,
                              path + "[" + String(i) + "].id",
                              "ID is duplicated in this scope.");
    }
}

void validateMemberShape(const AssemblyMembers &members, const String &path,
                         ValidationResult &result, size_t depth = 0)
{
    if (depth > MaxGraphEndpoints)
    {
        addDiagnostic(result, DiagnosticCode::CapacityExceeded, path,
                      "Assembly hierarchy depth exceeded.");
        return;
    }
    validateIds(members.assemblies, members.assemblyCount, path + ".assemblies", result);
    validateIds(members.devices, members.deviceCount, path + ".devices", result);
    validateIds(members.connectors, members.connectorCount, path + ".connectors", result);
    if (members.connectionCount != 0 && members.connections == nullptr)
        addDiagnostic(result, DiagnosticCode::InvalidProfile, path + ".connections",
                      "Non-zero count has a null array.");
    for (size_t i = 0; i < members.deviceCount && members.devices != nullptr; ++i)
    {
        validateIds(members.devices[i].terminals, members.devices[i].terminalCount,
                    path + ".devices[" + String(i) + "].terminals", result);
        for (size_t terminal = 0; terminal < members.devices[i].terminalCount; ++terminal)
            if (members.devices[i].terminals[terminal].canonicalNet > CanonicalNet::ProtectiveEarth)
                addDiagnostic(result, DiagnosticCode::InvalidValue,
                              path + ".devices[" + String(i) + "].terminals[" +
                                  terminal + "].canonical_net",
                              "Canonical net enum is invalid.");
    }
    for (size_t i = 0; i < members.connectorCount && members.connectors != nullptr; ++i)
    {
        validateIds(members.connectors[i].contacts, members.connectors[i].contactCount,
                    path + ".connectors[" + String(i) + "].contacts", result);
        if (members.connectors[i].kind > ConnectorKind::Generic)
            addDiagnostic(result, DiagnosticCode::InvalidValue,
                          path + ".connectors[" + String(i) + "].kind",
                          "Connector kind enum is invalid.");
        for (size_t contact = 0; contact < members.connectors[i].contactCount; ++contact)
            if (members.connectors[i].contacts[contact].canonicalNet > CanonicalNet::ProtectiveEarth)
                addDiagnostic(result, DiagnosticCode::InvalidValue,
                              path + ".connectors[" + String(i) + "].contacts[" +
                                  contact + "].canonical_net",
                              "Canonical net enum is invalid.");
    }
    for (size_t i = 0; i < members.connectionCount && members.connections != nullptr; ++i)
    {
        const EndpointRef endpoints[] = {members.connections[i].a, members.connections[i].b};
        for (size_t side = 0; side < 2; ++side)
            if (!validPath(endpoints[side].assembly, true) ||
                !validId(endpoints[side].owner) || !validId(endpoints[side].endpoint) ||
                endpoints[side].kind > EndpointKind::ConnectorContact)
                addDiagnostic(result, DiagnosticCode::InvalidValue,
                              path + ".connections[" + String(i) + "]",
                              "Connection endpoint is malformed.");
    }
    for (size_t i = 0; i < members.assemblyCount && members.assemblies != nullptr; ++i)
    {
        if (members.assemblies[i].kind > AssemblyKind::Generic)
            addDiagnostic(result, DiagnosticCode::InvalidValue,
                          path + ".assemblies[" + String(i) + "].kind",
                          "Assembly kind enum is invalid.");
        validateMemberShape(members.assemblies[i].members,
                            path + ".assemblies[" + String(i) + "]", result,
                            depth + 1);
    }
}

bool definitionCycleFrom(const Profile &config, const HardwareDefinition &start,
                         const AssemblyMembers &members, size_t depth)
{
    if (!present(start.id)) return false;
    if (depth > MaxGraphEndpoints) return true;
    if (members.assemblies == nullptr) return false;
    for (size_t i = 0; i < members.assemblyCount; ++i)
    {
        const Assembly &child = members.assemblies[i];
        if (present(child.definition))
        {
            if (!strcmp(child.definition, start.id)) return true;
            const HardwareDefinition *next = findDefinition(config, child.definition);
            if (next != nullptr &&
                definitionCycleFrom(config, start, next->members, depth + 1))
                return true;
        }
        if (definitionCycleFrom(config, start, child.members, depth + 1))
            return true;
    }
    return false;
}

void validateInstanceDefinitions(const Profile &config, const Assembly &assembly,
                                 const String &path, size_t depth,
                                 ValidationResult &result)
{
    if (depth > MaxGraphEndpoints)
    {
        addDiagnostic(result, DiagnosticCode::CapacityExceeded, path,
                      "Assembly traversal depth exceeded.");
        return;
    }
    const HardwareDefinition *definition = findDefinition(config, assembly.definition);
    if (present(assembly.definition) && definition == nullptr)
        addDiagnostic(result, DiagnosticCode::UnknownDefinition, path,
                      String("Unknown definition '") + assembly.definition + "'.");
    const AssemblyMembers *sets[2] = {
        definition != nullptr ? &definition->members : nullptr, &assembly.members};
    if (definition != nullptr)
    {
        for (size_t i = 0; i < assembly.members.assemblyCount; ++i)
            for (size_t defined = 0; defined < definition->members.assemblyCount; ++defined)
                if (!strcmp(assembly.members.assemblies[i].id,
                            definition->members.assemblies[defined].id))
                    addDiagnostic(result, DiagnosticCode::DuplicateMember, path,
                                  "Inline assembly duplicates a definition member.");
        for (size_t i = 0; i < assembly.members.deviceCount; ++i)
            for (size_t defined = 0; defined < definition->members.deviceCount; ++defined)
                if (!strcmp(assembly.members.devices[i].id,
                            definition->members.devices[defined].id))
                    addDiagnostic(result, DiagnosticCode::DuplicateMember, path,
                                  "Inline device duplicates a definition member.");
        for (size_t i = 0; i < assembly.members.connectorCount; ++i)
            for (size_t defined = 0; defined < definition->members.connectorCount; ++defined)
                if (!strcmp(assembly.members.connectors[i].id,
                            definition->members.connectors[defined].id))
                    addDiagnostic(result, DiagnosticCode::DuplicateMember, path,
                                  "Inline connector duplicates a definition member.");
    }
    for (const AssemblyMembers *members : sets)
        if (members != nullptr && members->assemblies != nullptr)
            for (size_t i = 0; i < members->assemblyCount; ++i)
                validateInstanceDefinitions(config, members->assemblies[i],
                                            joinedPath(path, members->assemblies[i].id),
                                            depth + 1, result);
}

int findAssemblyIndex(const TopologyGraph &graph, const String &path)
{
    for (size_t i = 0; i < graph.assemblyCount; ++i)
        if (graph.assemblies[i].path == path) return static_cast<int>(i);
    return -1;
}

int addAssembly(TopologyGraph &graph, const String &path, ValidationResult *result)
{
    const int existing = findAssemblyIndex(graph, path);
    if (existing >= 0) return existing;
    if (graph.assemblyCount >= MaxGraphAssemblies)
    {
        if (result) addDiagnostic(*result, DiagnosticCode::CapacityExceeded, path,
                                  "Topology assembly capacity exceeded.");
        return -1;
    }
    graph.assemblies[graph.assemblyCount].path = path;
    return static_cast<int>(graph.assemblyCount++);
}

int findNode(const TopologyGraph &graph, uint16_t assembly, EndpointKind kind,
             const char *owner, const char *endpoint)
{
    if (!present(owner) || !present(endpoint)) return -1;
    for (size_t i = 0; i < graph.nodeCount; ++i)
        if (graph.nodes[i].assembly == assembly && graph.nodes[i].kind == kind &&
            !strcmp(graph.nodes[i].owner, owner) &&
            !strcmp(graph.nodes[i].endpoint, endpoint))
            return static_cast<int>(i);
    return -1;
}

bool addNode(TopologyGraph &graph, uint16_t assembly, const String &assemblyPath,
             EndpointKind kind,
             const char *owner, const char *endpoint, CanonicalNet canonical,
             ValidationResult *result)
{
    if (findNode(graph, assembly, kind, owner, endpoint) >= 0)
    {
        if (result) addDiagnostic(*result, DiagnosticCode::DuplicateMember, assemblyPath,
                                  "Effective endpoint is duplicated by an instance and definition.");
        return false;
    }
    if (graph.nodeCount >= MaxGraphEndpoints)
    {
        if (result) addDiagnostic(*result, DiagnosticCode::CapacityExceeded, assemblyPath,
                                  "Topology endpoint capacity exceeded.");
        return false;
    }
    graph.nodes[graph.nodeCount++] = {assembly, kind, owner, endpoint, canonical};
    return true;
}

bool collectNodes(const Profile &config, const AssemblyView &view,
                  const String &path, TopologyGraph &graph, ValidationResult *result,
                  size_t depth = 0)
{
    if (depth > MaxGraphEndpoints)
    {
        if (result) addDiagnostic(*result, DiagnosticCode::CapacityExceeded, path,
                                  "Assembly hierarchy depth exceeded.");
        return false;
    }
    if (!arraysPresent(view.instance->members) ||
        (view.definition != nullptr && !arraysPresent(view.definition->members)))
    {
        if (result) addDiagnostic(*result, DiagnosticCode::InvalidProfile, path,
                                  "Non-zero member count has a null array.");
        return false;
    }
    bool ok = true;
    const int assemblyIndex = addAssembly(graph, path, result);
    if (assemblyIndex < 0) return false;
    const AssemblyMembers *sets[2] = {
        view.definition != nullptr ? &view.definition->members : nullptr,
        &view.instance->members};
    for (const AssemblyMembers *members : sets)
    {
        if (members == nullptr) continue;
        for (size_t i = 0; i < members->deviceCount; ++i)
            for (size_t endpoint = 0; endpoint < members->devices[i].terminalCount; ++endpoint)
            {
                const Terminal &terminal = members->devices[i].terminals[endpoint];
                ok = addNode(graph, static_cast<uint16_t>(assemblyIndex), path,
                             EndpointKind::DeviceTerminal,
                             members->devices[i].id, terminal.id,
                             terminal.canonicalNet, result) && ok;
            }
        for (size_t i = 0; i < members->connectorCount; ++i)
            for (size_t endpoint = 0; endpoint < members->connectors[i].contactCount; ++endpoint)
            {
                const ConnectorContact &contact = members->connectors[i].contacts[endpoint];
                ok = addNode(graph, static_cast<uint16_t>(assemblyIndex), path,
                             EndpointKind::ConnectorContact,
                             members->connectors[i].id, contact.id,
                             contact.canonicalNet, result) && ok;
            }
    }
    for (size_t i = 0; i < childCount(view); ++i)
    {
        const AssemblyView child = childAt(config, view, i);
        ok = collectNodes(config, child, joinedPath(path, child.instance->id),
                          graph, result, depth + 1) && ok;
    }
    return ok;
}

DiagnosticCode missingEndpointCode(const Profile &config, const EndpointRef &endpoint)
{
    const AssemblyView assembly = findAssembly(config, endpoint.assembly);
    if (assembly.instance == nullptr) return DiagnosticCode::AssemblyNotFound;
    if (endpoint.kind == EndpointKind::DeviceTerminal)
        return findDevice(assembly, endpoint.owner) == nullptr
                   ? DiagnosticCode::DeviceNotFound : DiagnosticCode::TerminalNotFound;
    return findConnector(assembly, endpoint.owner) == nullptr
               ? DiagnosticCode::ConnectorNotFound : DiagnosticCode::ContactNotFound;
}

bool addConnections(const Profile &config, const Connection *connections, size_t count,
                    const String &base, TopologyGraph &graph,
                    ValidationResult *result, const String &sourcePath)
{
    if (count != 0 && connections == nullptr) return false;
    bool ok = true;
    for (size_t i = 0; i < count; ++i)
    {
        const String aPath = joinedPath(base, connections[i].a.assembly);
        const String bPath = joinedPath(base, connections[i].b.assembly);
        const int aAssembly = findAssemblyIndex(graph, aPath);
        const int bAssembly = findAssemblyIndex(graph, bPath);
        const int aIndex = aAssembly < 0 ? -1 :
            findNode(graph, static_cast<uint16_t>(aAssembly), connections[i].a.kind,
                     connections[i].a.owner, connections[i].a.endpoint);
        const int bIndex = bAssembly < 0 ? -1 :
            findNode(graph, static_cast<uint16_t>(bAssembly), connections[i].b.kind,
                     connections[i].b.owner, connections[i].b.endpoint);
        const String path = sourcePath + "[" + String(i) + "]";
        if (aIndex < 0 || bIndex < 0)
        {
            if (result)
            {
                EndpointRef missing = aIndex < 0 ? connections[i].a : connections[i].b;
                const String absolute = aIndex < 0 ? aPath : bPath;
                missing.assembly = absolute.c_str();
                addDiagnostic(*result, missingEndpointCode(config, missing), path,
                              "Connection endpoint does not resolve.");
            }
            ok = false;
            continue;
        }
        if (aIndex == bIndex)
        {
            if (result) addDiagnostic(*result, DiagnosticCode::SelfConnection, path,
                                      "A connection must join two distinct endpoints.");
            ok = false;
            continue;
        }
        if (aAssembly != bAssembly &&
            (connections[i].a.kind != EndpointKind::ConnectorContact ||
             connections[i].b.kind != EndpointKind::ConnectorContact))
        {
            if (result) addDiagnostic(*result, DiagnosticCode::CrossAssemblyDeviceConnection,
                                      path, "Assembly crossings must connect two connector contacts.");
            ok = false;
            continue;
        }
        bool duplicate = false;
        for (size_t edge = 0; edge < graph.edgeCount; ++edge)
            if ((graph.edges[edge].a == aIndex && graph.edges[edge].b == bIndex) ||
                (graph.edges[edge].a == bIndex && graph.edges[edge].b == aIndex))
                duplicate = true;
        if (duplicate)
        {
            if (result) addDiagnostic(*result, DiagnosticCode::DuplicateConnection, path,
                                      "Physical connection is duplicated.");
            ok = false;
            continue;
        }
        if (graph.edgeCount >= MaxGraphEdges)
        {
            if (result) addDiagnostic(*result, DiagnosticCode::CapacityExceeded, path,
                                      "Topology connection capacity exceeded.");
            return false;
        }
        graph.edges[graph.edgeCount++] = {
            static_cast<uint16_t>(aIndex), static_cast<uint16_t>(bIndex)};
    }
    return ok;
}

bool collectConnections(const Profile &config, const AssemblyView &view,
                        const String &path, TopologyGraph &graph,
                        ValidationResult *result, size_t depth = 0)
{
    if (depth > MaxGraphEndpoints)
    {
        if (result) addDiagnostic(*result, DiagnosticCode::CapacityExceeded, path,
                                  "Assembly hierarchy depth exceeded.");
        return false;
    }
    if (!arraysPresent(view.instance->members) ||
        (view.definition != nullptr && !arraysPresent(view.definition->members)))
    {
        if (result) addDiagnostic(*result, DiagnosticCode::InvalidProfile, path,
                                  "Non-zero member count has a null array.");
        return false;
    }
    bool ok = true;
    if (view.definition != nullptr)
        ok = addConnections(config, view.definition->members.connections,
                            view.definition->members.connectionCount, path, graph, result,
                            String("definition:") + view.definition->id + ".connections") && ok;
    ok = addConnections(config, view.instance->members.connections,
                        view.instance->members.connectionCount, path, graph, result,
                        path + ".connections") && ok;
    for (size_t i = 0; i < childCount(view); ++i)
    {
        const AssemblyView child = childAt(config, view, i);
        ok = collectConnections(config, child, joinedPath(path, child.instance->id),
                                graph, result, depth + 1) && ok;
    }
    return ok;
}
}

const char *assemblyKindName(AssemblyKind kind)
{
    static const char *names[] = {"custom_board", "market_board", "module", "sensor_probe", "panel", "enclosure", "external", "generic"};
    const size_t index = static_cast<size_t>(kind);
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "generic";
}

const char *connectorKindName(ConnectorKind kind)
{
    static const char *names[] = {"header", "screw_terminal", "jst", "usb", "terminal", "direct_pin", "direct_wire", "generic"};
    const size_t index = static_cast<size_t>(kind);
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "generic";
}

const char *endpointKindName(EndpointKind kind)
{
    return kind == EndpointKind::DeviceTerminal ? "device_terminal" : "connector_contact";
}

const char *canonicalNetName(CanonicalNet net)
{
    static const char *names[] = {"", "GND", "VCC", "+3V3", "+5V", "AC_PHASE", "AC_NEUTRAL", "PE"};
    const size_t index = static_cast<size_t>(net);
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "";
}

const char *diagnosticCodeName(DiagnosticCode code)
{
    static const char *names[] = {"INVALID_PROFILE", "INVALID_ID", "DUPLICATE_ID", "UNKNOWN_DEFINITION", "DEFINITION_CYCLE", "DUPLICATE_MEMBER", "ASSEMBLY_NOT_FOUND", "DEVICE_NOT_FOUND", "CONNECTOR_NOT_FOUND", "TERMINAL_NOT_FOUND", "CONTACT_NOT_FOUND", "CROSS_ASSEMBLY_DEVICE_CONNECTION", "DUPLICATE_CONNECTION", "CANONICAL_NET_CONFLICT", "CAPACITY_EXCEEDED", "INVALID_VALUE", "SELF_CONNECTION"};
    const size_t index = static_cast<size_t>(code);
    return index < sizeof(names) / sizeof(names[0]) ? names[index] : "UNKNOWN";
}

bool buildTopologyGraph(const Profile &config, TopologyGraph &graph,
                        ValidationResult *diagnostics)
{
    graph.assemblyCount = 0;
    graph.nodeCount = 0;
    graph.edgeCount = 0;
    if ((config.definitionCount != 0 && config.definitions == nullptr) ||
        (config.rootCount != 0 && config.roots == nullptr) ||
        (config.connectionCount != 0 && config.connections == nullptr)) return false;
    bool ok = true;
    for (size_t i = 0; i < config.rootCount; ++i)
    {
        const AssemblyView root{&config.roots[i], findDefinition(config, config.roots[i].definition)};
        ok = collectNodes(config, root, String(config.roots[i].id), graph, diagnostics) && ok;
    }
    if (!ok) return false;
    for (size_t i = 0; i < config.rootCount; ++i)
    {
        const AssemblyView root{&config.roots[i], findDefinition(config, config.roots[i].definition)};
        ok = collectConnections(config, root, String(config.roots[i].id), graph, diagnostics) && ok;
    }
    if (!ok) return false;
    ok = addConnections(config, config.connections, config.connectionCount, "",
                        graph, diagnostics, "connections") && ok;
    return ok;
}

bool inferNets(const TopologyGraph &graph, InferredNets &nets)
{
    nets = InferredNets{};
    uint16_t parent[MaxGraphEndpoints];
    for (size_t i = 0; i < graph.nodeCount; ++i) parent[i] = static_cast<uint16_t>(i);
    for (size_t edge = 0; edge < graph.edgeCount; ++edge)
    {
        uint16_t a = graph.edges[edge].a;
        while (parent[a] != a) a = parent[a];
        uint16_t b = graph.edges[edge].b;
        while (parent[b] != b) b = parent[b];
        if (a != b) parent[b] = a;
    }
    uint16_t rootToNet[MaxGraphEndpoints];
    for (size_t i = 0; i < MaxGraphEndpoints; ++i) rootToNet[i] = UINT16_MAX;
    for (size_t i = 0; i < graph.nodeCount; ++i)
    {
        uint16_t root = static_cast<uint16_t>(i);
        while (parent[root] != root) root = parent[root];
        if (rootToNet[root] == UINT16_MAX)
            rootToNet[root] = static_cast<uint16_t>(nets.netCount++);
        const uint16_t net = rootToNet[root];
        nets.netByNode[i] = net;
        const CanonicalNet canonical = graph.nodes[i].canonicalNet;
        if (canonical == CanonicalNet::None) continue;
        if (nets.canonicalByNet[net] == CanonicalNet::None)
            nets.canonicalByNet[net] = canonical;
        else if (nets.canonicalByNet[net] != canonical)
            nets.conflictByNet[net] = true;
    }
    return true;
}

ValidationResult validateHwConfig(const Profile &config)
{
    ValidationResult result;
    validateIds(config.definitions, config.definitionCount, "definitions", result);
    validateIds(config.roots, config.rootCount, "roots", result);
    if (config.rootCount == 0 || config.roots == nullptr ||
        (config.connectionCount != 0 && config.connections == nullptr))
        addDiagnostic(result, DiagnosticCode::InvalidProfile, "",
                      "At least one root and valid counted arrays are required.");
    for (size_t i = 0; i < config.definitionCount && config.definitions != nullptr; ++i)
    {
        if (config.definitions[i].kind > AssemblyKind::Generic)
            addDiagnostic(result, DiagnosticCode::InvalidValue,
                          String("definitions[") + i + "].kind",
                          "Assembly kind enum is invalid.");
        validateMemberShape(config.definitions[i].members,
                            String("definitions[") + i + "]", result);
    }
    for (size_t i = 0; i < config.rootCount && config.roots != nullptr; ++i)
    {
        if (config.roots[i].kind > AssemblyKind::Generic)
            addDiagnostic(result, DiagnosticCode::InvalidValue,
                          String("roots[") + i + "].kind",
                          "Assembly kind enum is invalid.");
        validateMemberShape(config.roots[i].members,
                            String("roots[") + i + "]", result);
    }
    if (!validPath(config.hostAssembly, false))
        addDiagnostic(result, DiagnosticCode::InvalidValue, "host_assembly",
                      "Host assembly path is malformed.");
    for (size_t i = 0; i < config.connectionCount && config.connections != nullptr; ++i)
    {
        const EndpointRef endpoints[] = {config.connections[i].a, config.connections[i].b};
        for (size_t side = 0; side < 2; ++side)
            if (!validPath(endpoints[side].assembly, false) ||
                !validId(endpoints[side].owner) || !validId(endpoints[side].endpoint) ||
                endpoints[side].kind > EndpointKind::ConnectorContact)
                addDiagnostic(result, DiagnosticCode::InvalidValue,
                              String("connections[") + i + "]",
                              "Connection endpoint is malformed.");
    }

    if (!result.valid()) return result;

    for (size_t i = 0; i < config.definitionCount; ++i)
        if (definitionCycleFrom(config, config.definitions[i],
                                config.definitions[i].members, 0))
            addDiagnostic(result, DiagnosticCode::DefinitionCycle,
                          String("definitions[") + i + "]",
                          "Definition references form a cycle.");

    if (!result.valid()) return result;

    for (size_t i = 0; i < config.rootCount && config.roots != nullptr; ++i)
        validateInstanceDefinitions(config, config.roots[i], String(config.roots[i].id),
                                    0, result);
    for (size_t i = 0; i < config.definitionCount; ++i)
    {
        const Assembly definitionRoot("definition_root", config.definitions[i].id);
        validateInstanceDefinitions(config, definitionRoot,
                                    String("definition:") + config.definitions[i].id,
                                    0, result);
    }

    // Do not expand malformed or cyclic pointer graphs.
    if (!result.valid()) return result;
    TopologyGraph *graph = new (std::nothrow) TopologyGraph;
    InferredNets *nets = new (std::nothrow) InferredNets;
    if (graph == nullptr || nets == nullptr)
    {
        delete graph;
        delete nets;
        addDiagnostic(result, DiagnosticCode::CapacityExceeded, "",
                      "Could not allocate topology validation workspace.");
        return result;
    }
    // A reusable definition must be valid even when this deployment does not
    // instantiate it. Expand each one under a synthetic root before validating
    // the actual deployment graph.
    for (size_t i = 0; i < config.definitionCount; ++i)
    {
        const Assembly definitionRoot("definition_root", config.definitions[i].id);
        const Profile definitionProfile("definition_root", config.definitions,
                                        config.definitionCount, &definitionRoot, 1,
                                        nullptr, 0);
        buildTopologyGraph(definitionProfile, *graph, &result);
        inferNets(*graph, *nets);
        for (size_t net = 0; net < nets->netCount; ++net)
            if (nets->conflictByNet[net])
                addDiagnostic(result, DiagnosticCode::CanonicalNetConflict,
                              String("definition:") + config.definitions[i].id +
                                  ".inferred_nets[" + net + "]",
                              "Connected endpoints declare different canonical net identities.");
    }
    buildTopologyGraph(config, *graph, &result);
    inferNets(*graph, *nets);
    for (size_t i = 0; i < nets->netCount; ++i)
        if (nets->conflictByNet[i])
            addDiagnostic(result, DiagnosticCode::CanonicalNetConflict,
                          String("inferred_nets[") + i + "]",
                          "Connected endpoints declare different canonical net identities.");
    delete graph;
    delete nets;
    if (present(config.hostAssembly) &&
        findAssembly(config, config.hostAssembly).instance == nullptr)
        addDiagnostic(result, DiagnosticCode::AssemblyNotFound, "host_assembly",
                      "Host assembly does not resolve.");
    return result;
}

const char *assemblyModel(const Profile &config, const char *absolutePath)
{
    const AssemblyView view = findAssembly(config, absolutePath);
    if (view.instance == nullptr) return "unspecified";
    if (present(view.instance->model)) return view.instance->model;
    if (view.definition != nullptr && present(view.definition->model))
        return view.definition->model;
    return "unspecified";
}

Profile getProfile()
{
#if NM_HAS_PROJECT_HARDWARE
    return projectProfile();
#else
    static const Assembly roots[] = {{"main"}};
    return {"main", nullptr, 0, roots, 1, nullptr, 0};
#endif
}
}
