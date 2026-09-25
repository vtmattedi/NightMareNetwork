---
title: Hardware configuration v2 model
description: Physical composition, connector boundaries, wiring, reusable definitions, and inferred electrical nets.
section: architecture
order: 40
---

# Hardware configuration v2 model

Hardware configuration v2 is the firmware's current physical-hardware model and
the source-of-truth contract for editor implementation. It replaces the former
flat topology/positional MessagePack model. The v2 source document is intended
for maintenance, validation, tooling, and reconstruction of a deployed physical
system.

The four central concepts are:

```text
assemblies   physical composition
connectors   physical boundaries
connections actual wiring
inferred nets connected components of the endpoint graph
```

## Vocabulary

An **assembly** is a physically meaningful unit. A PCB is an assembly whose
`kind` is `custom_board` or `market_board`; boards are not a separate container
type. Enclosures, modules, probes, panels, and external equipment use the same
composition model.

A **device** is a functional physical component contained directly by one
assembly. It exposes **terminals**.

A **connector** is a physical boundary of an assembly. It exposes one or more
**contacts**. A soldered wire or PCB pad that leaves an assembly is represented
by `direct_wire` or `direct_pin`, so it remains visible as a maintenance point.

A **connection** is an undirected physical conductor between exactly two
endpoint references. A connection may carry optional wire metadata, but it does
not name an electrical net. Its two endpoint references must be different;
self-connections are invalid.

An **endpoint reference** is a structured stable identity:

```json
{
  "assembly": "controller/logic_board",
  "kind": "device_terminal",
  "owner": "mcu",
  "endpoint": "GPIO10"
}
```

`assembly` is the slash-delimited path of assembly instance IDs from a
deployment root. IDs cannot contain `/`. `owner`
is a device or connector ID in that exact assembly. `endpoint` is a terminal or
contact ID. Names are presentation metadata and are never used by references.

A **hardware definition** describes reusable, immutable physical structure. An
**assembly instance** describes a deployed physical unit and may refer to one
definition. Definition members and connections are expanded in the instance's
namespace. Instances may add members and wiring, but v2 deliberately has no
member override or inheritance mechanism. Hardware that differs materially
should use a different definition revision.

## Document shape

```json
{
  "version": 2,
  "host_assembly": "controller/esp32",
  "definitions": [],
  "roots": [],
  "connections": []
}
```

A deployment has one or more roots. Multiple roots represent separate physical
units without inventing a common enclosure. Nested assemblies represent real
physical containment, not electrical connectivity.

`host_assembly` is the absolute path of the assembly running this firmware and
feeds the board model in `/info`. Definitions use the same member shapes as instances and may contain nested
assembly templates. A nested template can itself reference another definition.
Recursive definition references are invalid.

## Physical-boundary invariant

Endpoints in the same exact assembly may connect freely. Endpoints in different
assemblies, including a parent and child assembly, may connect only when both
are connector contacts.

Therefore this is invalid:

```text
controller/mcu.GPIO10 -> probe/sensor.DATA
```

and this is valid:

```text
controller/mcu.GPIO10 -> controller/J3.2
controller/J3.2       -> probe/J1.2
probe/J1.2            -> probe/sensor.DATA
```

This rule applies to deployment wiring and reusable-definition wiring. There is
no exception for directly soldered wires; those exits are connectors.

## Inferred nets and canonical identities

Each terminal/contact is a graph node and each connection is an undirected
edge. Every connected component is an inferred electrical net. Normal nets are
never declared or manually maintained.

An endpoint may declare one canonical identity:

```text
GND VCC +3V3 +5V AC_PHASE AC_NEUTRAL PE
```

The identity propagates across its inferred net. Two different canonical
identities in one component are conservatively diagnosed as a conflict. A
defined conversion or isolation device must be represented by distinct
terminals; device behavior does not implicitly connect terminals.

Canonical identities are exact electrical identities, not classifications or
wildcards. In particular, `VCC` means a rail literally identified as `VCC` and
is distinct from both `+3V3` and `+5V`. When the positive rail is not known,
omit `canonical_net`; do not use `VCC` as “some positive supply.”

## Validation contract

`validateHwConfig()` returns a fixed-capacity `ValidationResult` containing
structured diagnostics with `code`, `path`, and `message`. It checks IDs and uniqueness, definition
references and cycles, endpoint existence, connector contacts, and the physical
boundary rule. It also builds the endpoint graph and reports canonical-net
contradictions when structural checks permit it.

Invalid connections, including cross-assembly device-terminal connections and
self-connections, are diagnosed and are never inserted into a graph returned by
`buildTopologyGraph()`.

Incomplete knowledge is valid: an assembly or definition may omit internal
details. References that are present must still resolve exactly.

## Deliberate v2 boundaries

Version 2 does not define overrides, definition inheritance, implicit pin
aliases, connector geometry, cable/harness manufacturing data, PCB routing,
simulation, or general electrical-rule checking. Physical location and serial
metadata are optional instance metadata. A future first-class harness can be
added as an assembly kind without changing the boundary rule.

The firmware reference implementation lives in
`src/NightMare/HardwareProfile.h` and `.cpp`. The public operations are:

```cpp
ValidationResult validateHwConfig(const Profile &config);
bool buildTopologyGraph(const Profile &config, TopologyGraph &graph,
                        ValidationResult *diagnostics = nullptr);
bool inferNets(const TopologyGraph &graph, InferredNets &nets);
```

`NightMare/HardwareDefinitions.h` supplies reusable definitions for the
ESP32-C3 SuperMini rev1, MycroftY controller rev1, a DS18B20 waterproof probe,
and a generic one-channel relay module. `standardDefinitions(count)` returns
the complete catalog; the MycroftY definition composes the ESP32 definition.

Firmware publishes only the source configuration at `<device>/hardware` as
retained JSON. Inferred nets are a generated view and are not serialized as
manually maintained source data. A compact hardware encoding is deliberately
deferred until the schema is stable.

## Serialized member shapes

The string values are fixed as follows:

```text
assembly kind
    custom_board, market_board, module, sensor_probe, panel,
    enclosure, external, generic

connector kind
    header, screw_terminal, jst, usb, terminal,
    direct_pin, direct_wire, generic

endpoint kind
    device_terminal, connector_contact
```

Definitions and assemblies contain `assemblies`, `devices`, `connectors`, and
`connections` arrays. The firmware always emits these arrays, including when
empty. A deployed assembly can name a reusable definition and add inline
members, but an inline member cannot replace a definition member with the same
ID.

Device terminals and connector contacts have an `id`, optional `name`, and
optional `canonical_net`. Endpoint references use the exact shape shown above.
Definition/assembly-local connection paths are relative to their containing
assembly; an empty path means that assembly. Top-level connection paths are
absolute from a root.

Connection wire metadata uses optional `color`, `gauge`, `label`, and
`length_mm` fields. Zero length is omitted.

A minimal reusable definition and deployment instance serialize as:

```json
{
  "version": 2,
  "host_assembly": "controller",
  "definitions": [{
    "id": "controller-v1",
    "kind": "custom_board",
    "model": "My controller",
    "assemblies": [],
    "devices": [{
      "id": "mcu",
      "kind": "mcu",
      "terminals": [
        {"id": "GPIO10"},
        {"id": "GND", "canonical_net": "GND"}
      ]
    }],
    "connectors": [{
      "id": "j1",
      "kind": "header",
      "contacts": [{"id": "1"}, {"id": "2", "canonical_net": "GND"}]
    }],
    "connections": [{
      "a": {"assembly": "", "kind": "device_terminal", "owner": "mcu", "endpoint": "GPIO10"},
      "b": {"assembly": "", "kind": "connector_contact", "owner": "j1", "endpoint": "1"}
    }]
  }],
  "roots": [{
    "id": "controller",
    "definition": "controller-v1",
    "assemblies": [],
    "devices": [],
    "connectors": [],
    "connections": []
  }],
  "connections": []
}
```

Optional metadata fields are omitted rather than serialized as `null`.

The firmware uses bounded graph/diagnostic storage (`MaxGraphAssemblies`,
`MaxGraphEndpoints`, `MaxGraphEdges`, and `MaxDiagnostics`). Each effective
assembly has one graph index and one stored diagnostic/lookup path;
`GraphNode::assembly` is that index, so endpoint nodes do not allocate duplicate
path Strings. Capacity exhaustion is a validation
error, never silent truncation. `TopologyGraph` and `InferredNets` are caller-
owned workspaces; firmware code should give long-lived analyses static storage
instead of placing both large fixed arrays on a small task stack.
