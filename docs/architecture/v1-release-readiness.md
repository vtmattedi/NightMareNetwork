---
title: v1.0 release readiness
description: Alpha-to-v1.0 blockers, contract freeze, integration validation, and release acceptance gates.
section: architecture
order: 40
---

# v1.0 release readiness

**Assessment date:** 2026-11-03  
**Baseline:** `main` at `f976b6afe20fcdfec6f91c1d482ae0d582162454`  
**Published version:** `v1.0.0-alpha.1`  
**Decision:** **Not yet ready for a stable v1.0 tag.** Treat the current functionality as feature-complete for the intended v1 scope; focus on correctness, bounded failure handling, tests, and protocol freeze.

This is a release-gate document, **not** a promise to implement every idea in [Known gaps](known-gaps.md). Findings distinguish source-confirmed issues from documented limitations and tests that have not yet been executed. Reassess them against the release-candidate commit before marking any gate as passed.

The v1 goal is to make the currently supported ESP32 C++ framework and its MQTT/ESP-NOW Resource protocol dependable. It is not to release every NightMare ecosystem application or a new Gateway hardware revision at the same time.

## Baseline and evidence

| Area | Observed on assessment date | Release consequence |
| --- | --- | --- |
| Library | `library.json` declares `1.0.0-alpha.1`; `main` last changed 2026-10-27 | Cut a release candidate only after the blockers below |
| Automated CI | `.github/workflows/website.yml` builds/checks the website; no library build/test workflow is present | Add required embedded CI |
| Smoke projects | `tests/pio_smoke_core`, `tests/pio_smoke_default`, `tests/pio_smoke_idf` exist | Run all supported configurations in clean CI |
| ESP-NOW tests | The separate [Gateway repository](https://github.com/vtmattedi/NightmareGateway) has `test/test_espnow_protocol/test_main.cpp` | Integrate testing with the release gate; verify hardware results |
| Gateway firmware | Gateway `master` uses the NMNW submodule pinned to `6a4695a`, behind the assessed library `main` primarily in website/docs/version changes | Pin the final release commit and rebuild |
| Backend | [MwNightmareSystem](https://github.com/vtmattedi/MwNightmareSystem) handles current MessagePack manifests and Gateway recovery documents | Execute end-to-end compatibility tests, not a new decoder project |
| GitHub | `main` was reported as unprotected; no open issues/PRs found | Track the milestone and require embedded CI |
| Alpha release metadata | GitHub release tagged `v1.0.0-alpha.1` has `prerelease: false` | Correct release metadata; mark RC releases as prereleases |

The [security review](../security/espnow_security_review.md), [ESP-NOW contract](../modules/espnow-protocol.md), [Network module](../modules/network.md), [Gateway module](../modules/gateway.md), and [design decisions](design-decisions.md) provide detailed background.

## Release-blocking correctness work

Use the priorities here as a proposed release backlog. A **P0** blocks the first release candidate; a **P1** must be resolved or explicitly removed from the v1 guarantee before stable release. Link each fix to a regression test.

### P0-01 — Observe Gateway MQTT control-plane traffic independently of the active Resource transport

**Source-confirmed:** `src/Network/MQTT/NmMqttConnection.cpp::messageReceived()` forwards MQTT messages only when `GetActiveConnection() == MQTT`. Meanwhile, Gateway candidate subscriptions are installed via `GatewayCandidateOnConnected()`, which uses the selected transport through `Subscribe()`.

**Failure mode:** a device on ESP-NOW may stop observing authoritative broker-side Gateway presence/readiness changes. This undermines later route-eligibility decisions.

**Acceptance:**
- Control-plane Gateway network/status subscription and ingress do not depend on which transport currently carries Resource traffic.
- Routing of ordinary application/Resource messages still uses exactly one active transport; no accidental duplicate delivery.
- A Gateway offline/status change is observed while ESP-NOW is active, and the coordinator reacts according to the documented policy.
- Reconnect/profile-change tests prove the subscriptions are restored through the correct underlying MQTT service.

### P0-02 — Make Gateway candidate compatibility and offline ordering correct

**Source-confirmed:** `src/Network/GatewayCandidate.cpp::GatewayCandidateProbable()` requires BSSID equality whenever both BSSIDs are available. The stated policy is to **prefer** matching BSSID, but fall back to matching SSID and channel where appropriate. Also, the status handler can clear `current.known` for an offline event, while a later same-generation retained `/gateway/network` message can mark the candidate known again.

**Acceptance:**
- Add table-driven cases for same/different BSSID, matching/different SSID, and same/different channel, matching the final documented eligibility policy.
- Authoritative offline status for a Gateway generation cannot be undone by stale readiness information from that generation.
- Old-generation offline updates cannot invalidate a newer live generation.
- Matching an announcement alone never authenticates an ESP-NOW Gateway; the secured-session/identity proof remains mandatory before switching a healthy MQTT fallback.

### P0-03 — Finish logical ESP-NOW MESSAGE outcome semantics

**Source-confirmed:** `src/Gateway/EspNow/espBroker.cpp::handleMessageFrame()` does not send a terminal ACK/ERROR for `MESSAGE`; `src/Network/EspNow/EspNowClient.cpp` currently ignores received `ACK` frames. SUBSCRIBE/UNSUBSCRIBE and LAST_WILL have different outcomes.

**Previously agreed gate:** one logical message ID should have one correlated terminal result, **not** one response per fragment.

**Acceptance:**
- Define precisely what a positive result means (e.g. accepted by the receiving bridge), without claiming remote command execution or exactly-once distributed delivery.
- Send one terminal result for a complete message and one well-defined error for recognized rejection/failure; correlate by session and message ID.
- Handle fragmented, timeout, duplicate and invalid messages consistently without accidental repeated application dispatch.
- Specify sender-side outcome tracking, retry/timeout policy and public error reporting; add tests on both sides.
- Update the frozen framing/error contract before changing the NM protocol version.

### P0-04 — Bound ESP-NOW reassembly and represent failures

**Source-confirmed:** Gateway `src/Gateway/EspNow/Reassembly.cpp` and device `src/Network/EspNow/EspNowClient.cpp` append to growing `std::vector` buffers. Gateway result types are `COMPLETE`, `INCOMPLETE`, and `DROPPED`, without explicit allocation-failure status.

**Acceptance:**
- Before allocating, validate framing, fragment count and device-specific receive budget.
- Reserve the bounded required capacity once and handle allocation failure without partial retained state or undefined behavior.
- Bound concurrent slots, timeouts and total receive memory. The wire-format maximum is not necessarily the minimum RAM capability of every board.
- Reject malformed/oversized/out-of-order messages with appropriate correlated failure behavior.
- Exercise near-capacity, intentionally missing, duplicated, out-of-order, and allocation-failure scenarios.

## Additional v1 stabilization

### P1-01 — Subscription acknowledgement and reconciliation

The protocol [documents](../modules/espnow-protocol.md) that a SUBSCRIBE lost inside an existing session is not retried until another transport restoration. The client ignores inbound ACKs.

**Acceptance:** a missed or rejected subscription has a bounded outcome and is retried/reconciled without requiring an unrelated disconnect. Confirm restoration after Gateway reboot, client reboot, and transport change.

### P1-02 — Broker retained tombstones after uplink outage

The [Gateway's current limits](../modules/gateway.md) acknowledge that a Resource tombstone applied while MQTT is down reaches local subscribers/vault but may leave the old broker-retained value in place.

**Acceptance:** a deletion made while disconnected survives in Gateway reconciliation state, is replayed on reconnect as an MQTT retained deletion, and cannot resurrect stale state. Test retained values and tombstones together.

### P1-03 — Finish the ESP-NOW security review's Fix Now bucket

The [security review](../security/espnow_security_review.md) identifies the following hardening tasks. The assessment found no confirmed complete implementation of A1-A4:

| ID | Requirement | Evidence / relevant source |
| --- | --- | --- |
| A1 | Bound failed-authentication attempts with abuse-resistant backoff | `Gateway/EspNow/espBroker.cpp::handleAuth()` |
| A2 | Enforce absolute pending-handshake lifetime; repeated CONNECT cannot indefinitely refresh a slot | `Gateway/EspNow/espDeviceManager.cpp::beginHandshake()` |
| A3 | Reject cheap malformed frames before consuming gateway RX queue capacity | `Gateway/EspNow/espBroker.cpp::recv_cb()` |
| A4 | Filter irrelevant control frames by source and current handshake/session state before the client control queue | `Network/EspNow/EspNowClient.cpp::receiveCallback()` |
| A5 | Explicit bounded fragmentation admission and allocation handling | Covered by P0-04 |

Preserve the stated threat model: PSK-authorized clients and the LAN are trusted; deliberate RF jamming is not solved by this release. The unauthenticated CONNECT session **replacement** issue is already addressed by verified AUTH; do not confuse the remaining bounded suspension/DoS limitation with the old session-destruction bug. Pre-auth error influence, fake BEACON steering and duplicate-message/idempotency tradeoffs may remain documented non-goals where appropriate.

## Implemented foundations to preserve, not rebuild

- Separate Wi-Fi radio, Wi-Fi IP, MQTT and ESP-NOW lifecycles, plus preference versus active routing.
- Sticky fallback and authenticated Gateway identity checking; eligibility still needs P0-01/P0-02.
- Remote/local MQTT profiles using one MQTT service; a Gateway can advertise its active uplink.
- Verified ESP-NOW AUTH session replacement and encrypted PING/PONG before CONNECTED.
- Gateway generation, retained client snapshot, matching-generation Last Will recovery and status/network reassertion.
- Managed Resources, Remote Resources, MessagePack-first framework documents, freshness/availability separation, and typed Configs.
- Scoped Scheduler jobs and persistence boundaries.
- Version-aware Gateway HTTP vault polling and HTTP timeout/socket-limit handling.
- Backend MessagePack decoding of Resource manifests and hardware topology; Gateway recovery handling.

An implemented mechanism is **not** a passing hardware test. Its behavior must be verified against the release candidate.

## Protocol and public API freeze

Freeze the following at the release candidate, after the correctness changes above:

| Surface | Freeze rule |
| --- | --- |
| Public `ConnectionType` | `AUTO=0`, `MQTT=1`, value 2 reserved, `ESP_NOW=3` |
| MQTT profiles | `REMOTE`/`LOCAL` are policies of the one MQTT service; preference does not enable/disable services |
| Routing | One active Resource transport; control-plane observation is separate; healthy fallback is sticky until preferred path is eligible |
| Gateway MQTT documents | `<id>/status`, `<id>/gateway/network`, `<id>/gateway/clients`; generation and offline precedence must be unambiguous |
| ESP-NOW framing | 10-byte packed frame header and explicit errors, session ID and message-ID correlation; V2 framing remains reserved |
| NM protocol version | Currently `0` / pre-v1. Advance only on explicit wire freeze, with compatibility/rejection tests |
| Resources | Canonical MessagePack documents, manifest encoding, `/state`, `/event`, `/set`, `/invoke` |
| State semantics | Retained values are last known, not presence/freshness or proof of remote execution |
| Persistence | Config and Scheduler current formats; no obligation to migrate pre-freeze protocols |

The v1 release does **not** promise transparent backward compatibility with earlier experimental protocol formats.

## Required automated gates

Add a library workflow (separate from the existing website workflow) and make it a required merge check. At minimum:

- [ ] Build `tests/pio_smoke_core` for Arduino.
- [ ] Build `tests/pio_smoke_default` variants: default features, optional servers, JSON wire, connectivity coexistence and logging.
- [ ] Build `tests/pio_smoke_idf`: core, full, servers and ESP-NOW.
- [ ] Build the released `examples/basic` and `examples/temp-sensor` from a clean checkout, with documented non-secret test configuration.
- [ ] Run protocol tests covering framing, crypto known-answer vectors, session replacement and bounded reassembly.
- [ ] Run routing-policy tests including Gateway eligibility, status generation order and preference/failover.
- [ ] Run tests for MessagePack/Resource semantics and idempotent reconnect publications.
- [ ] Preserve the existing website typecheck, build and docs-link verification.
- [ ] Protect the merge target with required embedded CI; ensure no test depends on untracked local headers or specific Windows paths.

A compile-only smoke project must be reported as **build coverage**, not as a passing device-level integration test.

## Required physical integration gates

Use a real ESP32-S3 Gateway, at least one ESP32-C3 client and a representative application/device Resource consumer. The current Gateway and backend projects are integration targets, not necessarily independent release blockers.

### Connectivity and recovery

- [ ] ESP-NOW preferred → MQTT fallback after ESP-NOW loss; retained preference unchanged.
- [ ] Healthy MQTT fallback → ESP-NOW only after valid readiness and authenticated session.
- [ ] Gateway readiness/status transitions are observed even while ESP-NOW is active.
- [ ] Remote/local broker profile changes and outages do not create duplicate Resource routing.
- [ ] Client reboot restores subscriptions, Last Will and Resources after secure session replacement.
- [ ] Gateway reboot generation is recognized; fallback heartbeat path also works.
- [ ] Wi-Fi scan, AP/channel change, broker restart and TLS reconnect recover without stuck services.

### Protocol, Resource and security

- [ ] Wrong PSK never establishes an authenticated session.
- [ ] Spoofed CONNECT cannot replace an authenticated session; failed handshakes expire predictably.
- [ ] Fragment losses/duplicates/reordering/oversized payloads have bounded outcomes and stable memory use.
- [ ] Logical `MESSAGE` completion, error and timeout semantics match the frozen contract.
- [ ] Dropped SUBSCRIBE converges without another disconnect.
- [ ] Offline retained tombstones reconcile with the broker after reconnect.
- [ ] Resource Values, Events, Actions, manifests and remote bindings survive the transport/reconnect matrix.
- [ ] Last Will and status do not falsely imply fresh Resource data.

### Release quality

- [ ] Clean tagged build of the Gateway using the final NMNW submodule commit.
- [ ] Backend end-to-end tests for MessagePack documents and status/generation recovery.
- [ ] Fresh-device quick start succeeds using published instructions.
- [ ] Record free heap, minimum heap, largest block, stack high-water marks and reconnect/failure counters during a **24-hour soak test** (proposed acceptance duration).
- [ ] Release critical tests pass and their firmware versions, boards, environment and results are recorded.

## Release sequence

1. **Stabilization branch/workstream:** resolve P0-01 through P0-04 and the P1 requirements, each with regression evidence. Do not add unrelated feature work.
2. **`v1.0.0-rc.1`:** freeze wire/API contracts, align documentation and consumers, pin dependency versions and turn on embedded CI/branch protection.
3. **Hardware sign-off:** run the matrix on actual boards and broker/backend; record results and fix any release-critical failures.
4. **`v1.0.0`:** set `library.json` version, publish a correctly classified stable release with a changelog, tag the tested commit, update Gateway submodule, and verify installation from the tag.

Do **not** label a release candidate or the final version stable solely because builds compile.

## Explicitly deferred from v1.0

These items are not release gates unless they expose a defect in an advertised v1 contract:

- W5500 Ethernet, microSD, replacement ESP32-S3 antenna module, or other Gateway hardware revisions.
- ESP-NOW V2 / 1470-byte runtime framing.
- Embedded local MQTT broker, Gateway database/SD, or broad Gateway feature additions.
- A centralized Runtime execution queue or generalized distributed transaction system.
- MQTT namespace abstraction, projected structured remote-value fan-out, or new Resource abstractions.
- Scheduler captured callback/context support, broader persistence migrations, or cross-platform portability beyond the documented ESP32 scope.
- New OTA capabilities, LVGL/HTTP/WebSocket architecture rewrites, and unrelated dashboard/editor UX.
- Application-specific Watson classifier tuning or new NightMare device features.

## Sign-off record

Complete this section *on the release candidate*, with links to CI runs, hardware logs and tracked fixes.

| Gate | Status | Evidence |
| --- | --- | --- |
| P0 correctness closed | Not verified | — |
| P1/security review closed or explicitly scoped out | Not verified | — |
| Public API and wire contract frozen | Not verified | — |
| Embedded CI green at exact release SHA | Not verified | — |
| Gateway/backend/device compatibility verified | Not verified | — |
| Hardware fault matrix and soak test passed | Not verified | — |
| Quick start from immutable tag verified | Not verified | — |

**Stable v1.0 may be published only after all applicable gates pass or their scope is explicitly adjusted and documented.**
