---
title: Namespaces (design)
description: Plan for isolating separate NightMare installations on one broker. Covers topics, gateway binding, the ESP-NOW namespace key, relay preparation, backend scoping and rollout.
section: architecture
order: 40
---

# Namespaces (design)

Status: **planned**, agreed 2026-11-23. Nothing here is implemented yet. Each
phase updates the protocol docs it changes: [topics](../protocols/topics.md),
the [ESP-NOW protocol](../modules/espnow-protocol.md) and the
[security review](../security/espnow_security_review.md).

## Goal

Several independent NightMare installations share one MQTT broker without
seeing or controlling each other. Examples are a home and the Turing plant
bench. The broker also carries services that are not NightMare
(stackport-wss-mqtt-bridge, Stackport host state), so NightMare must also stay
apart from those.

## Terms

- **Namespace.** One isolated installation. It is the unit of security, on the
  broker and on ESP-NOW. It is also the unit of ownership in the backend, where
  namespace = project. End users never see the word: the app shows only the
  namespace's display name, such as "House" or "Turing".
- **Namespace ID.** A short slug: 1–16 characters from `[a-z0-9-]`, starting
  with a letter. It is short on purpose, because it is part of every MQTT topic.
- **Topic root.** A fixed first segment that separates NightMare from other
  users of the broker. It is configurable and defaults to `nm`.
- **Rooms.** Rooms are handled by name for now: a free-text room on a device or
  resource, used only for grouping in the app. They are not a protocol concept.

## 1. Topic layout

```text
<root>/<namespace>/<address>/...      e.g. nm/home/Esp32-nm-c4d2e801/status
<root>/<namespace>/all/console/in
<root>/<namespace>/Control/request
<root>/<namespace>/Control/time
```

Everything below `<root>/<namespace>/` is the current protocol, unchanged. An
empty namespace means the legacy unprefixed layout. It exists only for
migration.

### Topics are namespace-relative inside the library

All library code, resources, the router and applications keep building and
matching **relative** topics (`<address>/...`, `Control/time`). The prefix
exists only at the two MQTT client boundaries, where the library publishes,
subscribes, unsubscribes, sets the Last Will and receives:

- **Devices:** `src/Network/MQTT/NmMqttEsp.cpp`.
- **Gateway:** `src/Gateway/EspMqtt/espMqtt.cpp`. This is the gateway's own
  client. It subscribes to `#` today, and that becomes `<root>/<ns>/#`.

Both share one small prefix helper and apply the same rules:

- **publish, subscribe, unsubscribe and the Last Will:** prepend `<root>/<ns>/`;
- **receive:** strip the prefix. Drop, and count, any message that is not under
  the namespace.

Effects:

- Remote resources, `all/console/in` and `Control/*` stay inside the namespace
  with no further code.
- Application filters such as Dash-P4's `+/status` and `+/resource/+/state`
  become namespace-scoped automatically.
- A topic outside NightMare needs an explicit escape:
  `NightMare::SubscribeGlobal()` and `NightMare::PublishGlobal()` take an
  absolute topic, and the broker ACL must allow it. Dash-P4's `Control/forecast`
  (answered by the backend) stays relative, so it becomes namespace-scoped.
- The MQTT topic limits still apply to the full topic. The prefix costs at most
  `len(root) + 17` bytes.

### ESP-NOW stays relative

An ESP-NOW MESSAGE stores its topic length in 6 bits (`MaxTopicLength = 63`,
`Gateway/Broker/Topic.h`). Children therefore never see the prefix. The
gateway's MQTT client (`espMqtt.cpp`) adds and strips the prefix for its
children. Every hop below the gateway keeps the full
63-byte budget.

## 2. Gateway binding

A gateway belongs to exactly one namespace. It holds two secrets for it:

1. the namespace's **broker credentials** (assumed to be provisioned correctly
   until the broker phase);
2. the namespace's **ESP-NOW key**.

The gateway is never namespace-agnostic. An agnostic gateway would publish any
child that holds a valid key into its own namespace. With one shared ESP-NOW
key, a board from one home could then join another home's gateway.

## 3. ESP-NOW namespace key

### How it works today

`NM_ESPNOW_PSK` is a 32-character text string in each project's `creds.h`. The
gateway and every device hold the same value. The proof, the PMK and the LMK
are HMAC-SHA256 values keyed with it, using the labels `NM-AUTH`, `NM-PMK` and
`NM-LMK` (`Network/EspNow/NightMareEspNow/Auth.*`).

### The change

There is **one key per namespace**. This mirrors MQTT, where a namespace role
`nm/<ns>/#` isolates the namespace.

- **Issuing.** For now, by hand: the key is generated at random in the same
  format and kept in `creds.h`. The current installation becomes namespace
  `home` and keeps today's key. Generating and storing keys moves to the super
  admin in the broker phase.
- **Derivation.** The namespace ID is bound into every derivation, so a proof
  made in one namespace never verifies in another, even when two keys collide:

  ```text
  PMK   = first16(HMAC-SHA256(nsKey, "NM-PMK"  | nsId))
  proof = first16(HMAC-SHA256(nsKey, "NM-AUTH" | nsId | clientMac | gatewayMac | clientNonce | gatewayNonce))
  LMK   = first16(HMAC-SHA256(nsKey, "NM-LMK"  | nsId | clientMac | gatewayMac | clientNonce | gatewayNonce))
  ```

- **Namespace hint.** CONNECT carries `nsTag = first4(SHA-256(nsId))`. A gateway
  of another namespace ignores the request without running a handshake, so
  neighbours don't produce auth-failure noise. The hint is not a security
  boundary; the proof is.
- **Client MAC as a field.** CONNECT carries `clientMac`. The proof and LMK use
  this field, not the radio sender address. This is the main relay preparation
  (section 4).
- **Versioning.** The NM protocol nibble in `FrameHeader.version` goes from 0 to
  1, because the CONNECT payload grows. A gateway with a namespace speaks only
  v1. A gateway without one (legacy) speaks only v0. A namespace migrates as a
  whole: its gateway and its children are flashed together.
- **Deferred.** Per-device keys can come later without changing frames:
  `deviceKey = HMAC(nsKey, "NM-DEV" | nsId | clientMac)`, and the gateway derives
  it from the CONNECT field. Until then, revoking one board means rotating the
  namespace key.

## 4. Relay preparation (R1, not built yet)

The target is **R1, a trusted relay**. It is a member of the namespace and
authenticates to the gateway like a client. Toward its children it acts like a
small gateway without MQTT. It never holds the namespace key: it forwards a
child's CONNECT and AUTH to the gateway, which verifies the child and returns
the child's session key over the relay's own encrypted session.

This round builds only what keeps R1 possible without breaking existing
devices:

1. **Routing by client.** The gateway's session table (`espDeviceManager`) keys
   a session by `clientMac` and records a separate `linkMac`, the radio peer
   that frames go to. Today the two are always equal.
2. **Client MAC in the handshake.** See section 3.
3. **Reserved protocol space.** A `FORWARD` frame type (an envelope carrying
   `clientMac` and one inner frame) and a `RELAY` capability bit are reserved
   and documented. The 10-byte `FrameHeader` does not change. A gateway answers
   `FORWARD` with `UNSUPPORTED_VERSION` until R1 exists.
4. **Topics stay relative below the gateway** (section 1). Each hop keeps 63
   bytes.

## 5. Backend (after the library and firmware)

- **Wire.**
  - `parseTopic` strips `<root>/<ns>/` and returns the namespace together with
    today's parse.
  - Commands publish with the prefix.
  - The MQTT subscriptions become `<root>/+/...`.
  - While the home installation is still unprefixed, its legacy topics map to
    namespace `home`.
- **Data.**
  - a `namespaces` table (ID, display name);
  - devices and everything keyed by address move to `(namespace, address)`;
  - existing rows migrate to `home`.
- **Access:**

  | Role | Scope |
  |---|---|
  | Super admin | all namespaces, users, system, and later broker issuance |
  | Namespace admin | devices, cleanup and adopt, rooms, members and what members see, in their namespaces |
  | User | the resources marked visible for members; simple actions such as reboot; no IP, MAC, telemetry, console or hardware |

  Membership is `namespace_members(user, namespace, role)`, and a user may
  belong to several namespaces. Every route, realtime channel, notification
  rule and MCP token is scoped to a namespace.
- **App.**
  - a namespace switcher, showing display names only;
  - a home screen per role: all namespaces at a glance, namespace health plus
    the dashboard, or the dashboard only;
  - a simplified device page for users;
  - a members and visibility editor;
  - rooms grouped by name.

## 6. Broker (last)

The broker phase starts only after everything above works with credentials
provisioned by hand.

- **stackport-mqtt `nightmare` profile.**
  - Replace the single `nmnw` role on `#` with one role `nm-<ns>` per namespace,
    on `<root>/<ns>/#`.
  - Add a backend role on `<root>/#`.
  - Add explicit grants for any global topic a board uses.
- **Super admin issuance** through the stackport-mqtt admin API:
  - create a namespace's role;
  - create gateway and Wi-Fi device users, with the password shown once;
  - revoke a user;
  - generate the namespace ESP-NOW key and store it encrypted.

  First verify that the backend's admin token can be limited to `nm-*` users and
  roles, because the broker hosts other services.

## 7. Rollout

1. **Library ships with an empty namespace,** so behaviour is identical. Bump
   the consumers: device `lib_deps`, the Holmes submodule and Dash-P4. All
   still use legacy topics, and the live app keeps working.
2. **Validate on a bench namespace** (`turing`, not deployed yet). Give it a new
   gateway and child with a new key, and check isolation with an MQTT client.
   The home installation stays legacy.
3. **Backend** gains prefix parsing, namespaces and roles, and still maps
   legacy topics to `home`.
4. **Move home.** Flash Holmes and its children with namespace `home` and the
   v1 handshake, keeping today's key. Retire the old `MwNightmareSystem`
   (Stackport project 33), which only understands unprefixed topics.
5. **Broker phase** (section 6). After it, remove the legacy mapping.

## Open points

- **Trust inside a namespace is deliberate, and out of scope.** Every member of
  a namespace is trusted, and a gateway forwards whatever topics a child
  publishes. This is needed: device A changes device B, and any HMI controls
  any device. Profiles that allow or deny changes may come later.
- Dash-P4's `state/host` comes from the Stackport bridge, not from NightMare.
  Confirm whether it uses the NightMare MQTT connection; if it does, it needs
  `SubscribeGlobal`.
