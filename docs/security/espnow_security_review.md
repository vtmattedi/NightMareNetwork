
# NMNW ESP-NOW Security Review

## Scope

This document summarizes the current NightMare Network ESP-NOW communication model, the security concerns reviewed, and the decisions made for each concern.

The analysis intentionally uses a narrow threat model.

### Assumptions

For this review:

- The normal IP/Wi-Fi/LAN/MQTT network is considered trusted.
- Devices already authenticated into NMNW are considered trusted protocol participants.
- The NMNW PSK is considered cryptographically strong and secret.
- Physical access to devices is out of scope.
- Raw 2.4 GHz interference is out of scope as a solvable protocol problem.
  - An attacker in radio range can always transmit enough RF traffic to degrade or deny service.
  - NMNW cannot cryptographically prevent RF jamming or channel saturation.
- The primary attacker considered is a nearby, unauthenticated ESP-NOW participant that:
  - does not know the PSK;
  - does not possess a valid session LMK;
  - may transmit arbitrary plaintext ESP-NOW frames;
  - may spoof source MAC addresses;
  - may replay previously observed plaintext frames.

The objective is therefore to prevent unauthenticated participants from:

- authenticating as a legitimate device or gateway;
- injecting authenticated NMNW traffic;
- replaying authenticated messages;
- causing unnecessary memory/resource exhaustion;
- cheaply causing protocol-level denial of service beyond unavoidable RF interference.

---

# 1. Current ESP-NOW Communication Model

## 1.1 Frame format

Current V1 ESP-NOW packets use a packed 10-byte header:

```cpp
struct FrameHeader
{
    uint16_t messageId;
    uint8_t version;
    uint8_t type;
    uint16_t cid;
    uint8_t frameIndex;
    uint8_t totalFrames;
    uint16_t length;
} __attribute__((packed));
```

ESP-NOW V1 allows a maximum packet size of 250 bytes.

Therefore:

```text
Frame header:        10 bytes
Maximum frame data: 240 bytes
```

`totalFrames` and `frameIndex` are 8-bit values.

A logical NMNW message may therefore contain up to:

```text
255 × 240 = 61,200 bytes
```

The protocol allows this full message size.

Individual devices are allowed to support a smaller receive capacity.

---

## 1.2 Main frame types

The current protocol includes:

```text
BEACON
CONNECT
CHALLENGE
AUTH
CONNACK
DISCONNECT

PING
PONG

SUBSCRIBE
UNSUBSCRIBE
MESSAGE
LAST_WILL

ACK
ERROR
```

Handshake frames use `cid = 0`.

Established session traffic uses a gateway-assigned non-zero CID.

---

## 1.3 Discovery

The gateway periodically broadcasts plaintext `BEACON` frames.

A searching client may discover a gateway from one of these beacons and begin a handshake.

A BEACON is not authenticated.

This is intentional.

Discovery is therefore advisory only and never establishes trust.

---

## 1.4 Authentication handshake

The current authentication flow is conceptually:

```text
Client                         Gateway

CONNECT
clientNonce
----------------------------->

                               generate fresh gatewayNonce

                 CHALLENGE
                 gatewayNonce
<-----------------------------

AUTH
HMAC proof
----------------------------->

                               verify HMAC
                               derive session LMK

                 CONNACK
                 CID
<-----------------------------

install LMK                    install LMK

encrypted PING
----------------------------->

                 encrypted PONG
<-----------------------------

CONNECTED
```

The authentication proof is derived using the network PSK and handshake context containing:

```text
client MAC
gateway MAC
client nonce
gateway nonce
```

Both sides derive the session LMK from the same authenticated context.

The first encrypted PING/PONG exchange confirms that both sides derived and installed the same LMK.

---

## 1.5 Session encryption

Established ESP-NOW peer communication uses the session LMK through ESP-NOW's encrypted peer support.

This gives NMNW:

- encrypted session traffic;
- frame integrity;
- sender authentication at the ESP-NOW encryption layer;
- CCMP replay protection.

A new authenticated session derives a new LMK.

---

## 1.6 Message fragmentation

Large `MESSAGE` payloads are split into frames sharing the same `messageId`.

Gateway reassembly currently identifies an in-flight message using:

```text
sender MAC
+
CID
+
messageId
```

Fragments must arrive in order.

A reassembly slot records:

```text
MAC
CID
messageId
nextFrame
totalFrames
start time
buffer
```

The gateway currently maintains a small fixed number of concurrent reassembly slots.

---

# 2. Security Concerns Raised

During review, the following concerns were considered:

1. Unauthenticated CONNECT disrupting an established session.
2. Pending-handshake table exhaustion.
3. Fake BEACON steering.
4. Plaintext pre-auth ERROR spoofing.
5. RX queue amplification from malformed frames.
6. Client control-queue flooding.
7. Fragmented-message allocation and allocation failure.
8. Duplicate logical MESSAGE execution.
9. Same-session replay.
10. Cross-session replay.
11. Handshake replay.
12. Source-MAC spoofing.
13. Fake gateway impersonation.
14. CID/messageId reuse across sessions.
15. Exponential backoff itself becoming a denial-of-service primitive.

These concerns were separated into three buckets.

---

# 3. Security Buckets

## Bucket A — Fix Now

Issues that are either concrete protocol robustness problems or cheap denial-of-service mechanisms worth addressing immediately.

Current items:

```text
A1. Failed-authentication backoff
A2. Pending-handshake admission hardening
A3. Early malformed-frame rejection
A4. Client control-queue prefiltering
A5. Explicit fragmented-message admission/allocation failure
```

---

## Bucket B — Acknowledge and Revisit Later

Known concerns that do not currently break authentication or confidentiality and are not important enough to block the current implementation.

Current items:

```text
B1. Pre-auth plaintext ERROR influence
B2. Duplicate logical MESSAGE/idempotency semantics
B3. Fake BEACON steering
```

---

## Bucket C — Already Solved by Existing Design

Concerns that were investigated but are already handled by the cryptographic/session architecture.

Current items:

```text
C1. Same-session encrypted replay
C2. Source-MAC spoofing of authenticated traffic
C3. Cross-session replay
C4. Handshake replay
C5. Fake gateway authentication
C6. CID/messageId reuse across different sessions
```

---

# 4. Bucket A — Fix Now

## A1. Failed-Authentication Backoff

### Concern

An unauthenticated participant can repeatedly initiate authentication attempts.

Even though authentication will fail without the PSK, repeated attempts still consume:

- gateway CPU;
- handshake table entries;
- HMAC computation;
- peer operations;
- response traffic.

### Proposed solution

Implement exponential backoff for repeated authentication failures.

Conceptually:

```cpp
backoff =
    min(BASE_BACKOFF_MS << failureCount,
        MAX_BACKOFF_MS);
```

Example progression:

```text
1st failure     1 s
2nd failure     2 s
3rd failure     4 s
4th failure     8 s
5th failure    16 s
...
maximum        30–60 s
```

Add a small random jitter:

```cpp
backoff += esp_random() % JITTER_MS;
```

This avoids synchronized retries from multiple legitimate devices.

### Critical rule

The exponential penalty must only grow from a failure that the gateway itself can meaningfully attribute to an authentication attempt.

It must **not** be increased from arbitrary unauthenticated client-side events.

In particular:

```text
fake BEACON
missing CHALLENGE
handshake timeout
spoofed plaintext ERROR
general packet loss
```

must not create a long-lived exponential penalty on the client.

Otherwise an attacker could weaponize the backoff system itself.

Preferred semantics:

```text
Gateway receives bad AUTH proof from MAC
    ↓
increment per-MAC auth failure counter
    ↓
apply backoff/rate limit

Client receives no CHALLENGE
or handshake simply times out
    ↓
normal bounded retry
    ↓
no trusted auth-failure penalty
```

The failure counter should reset only after a successfully authenticated and secured session.

---

## A2. Pending-Handshake Admission Hardening

### Concern

The gateway currently has a fixed-size pending-handshake table.

This is already bounded, which prevents arbitrary memory exhaustion.

However, unauthenticated CONNECTs can occupy those slots.

A nearby participant could use several source MAC addresses to keep the table full and prevent legitimate clients from starting new handshakes.

### Proposed solution

Maintain explicit pending-handshake admission rules.

#### Hard maximum

Keep a small compile-time maximum.

Example:

```cpp
static constexpr size_t MaxPending = 3;
```

No dynamic growth.

---

### One pending handshake per MAC

A MAC may have at most one pending handshake.

Repeated CONNECTs from the same MAC must:

```text
reuse/replace that MAC's existing slot
```

and must never consume an additional slot.

---

### Stricter handshake timeout

The current 5-second timeout is generous for a local ESP-NOW exchange.

Use a shorter absolute handshake lifetime where practical, likely around:

```text
1–2 seconds
```

based on measured real-world timing.

---

### Do not permit infinite slot refresh

A repeated CONNECT from the same MAC must not allow the attacker to keep its slot alive forever simply by resetting:

```cpp
startedAtMs = now;
```

The handshake should have an absolute lifetime.

Example:

```text
first CONNECT
    ↓
allocate pending slot
    ↓
absolute expiry starts

repeated CONNECT
    ↓
may replace nonce/challenge state if needed
    ↓
does NOT indefinitely extend absolute expiry
```

---

### Combine with per-MAC auth backoff

A MAC repeatedly failing authentication should eventually be prevented from immediately acquiring another pending slot.

---

## A3. Reject Malformed Frames Before Expensive Processing

### Concern

The gateway receive callback currently accepts any ESP-NOW packet that is within the basic packet-size limit, copies it into an RX structure, queues it, wakes the processing task, and only later runs full NMNW frame validation.

An unauthenticated sender can therefore turn malformed traffic into:

```text
Wi-Fi callback
→ memcpy
→ queue insertion
→ task wake
→ dequeue
→ validation
→ discard
```

This does not bypass authentication and queue memory is bounded, but it unnecessarily amplifies garbage traffic.

### Proposed solution

Perform cheap, stateless validation before the expensive queue path.

The callback may reject immediately if any of these are obviously invalid:

```text
packet shorter than FrameHeader
packet larger than V1 maximum
unsupported frame version
unknown frame type
header.length inconsistent with received size
totalFrames == 0
frameIndex >= totalFrames
fragmentation used on a non-MESSAGE type
obviously invalid CID rules
wrong fixed-size control payload
```

The receive callback should still remain lightweight.

Do not perform there:

- allocation;
- HMAC;
- logging-heavy work;
- session mutation;
- blocking operations.

The principle is:

> Reject malformed traffic before spending queue/task resources on it.

---

## A4. Client Control-Queue Prefiltering

### Concern

The client currently has a small control queue for:

```text
CHALLENGE
CONNACK
PONG
ERROR
```

The queue depth is intentionally small.

If syntactically valid but irrelevant control frames are queued before sender/state checks, an unauthenticated nearby sender may fill the queue and cause the real gateway's response to be dropped.

Example:

```text
bogus control frames
    ↓
control queue full
    ↓
real CONNACK/PONG arrives
    ↓
queue insertion fails
    ↓
handshake/heartbeat timeout
```

### Proposed solution

Perform as much cheap relevance filtering as possible before queueing.

Examples:

#### CHALLENGE

Only queue while the client is actually waiting for a challenge.

Where available, also require the expected CONNECT `messageId`.

---

#### CONNACK

Only queue when:

```text
state == AUTHENTICATING
sender MAC == selected gateway MAC
messageId == expected authId
```

---

#### PONG

Only queue when:

```text
sender MAC == selected gateway MAC
CID == current CID
messageId == expected pingId
```

---

#### ERROR

At minimum require:

```text
sender MAC == selected gateway MAC
```

and reject errors irrelevant to the current client state before consuming queue capacity where practical.

The goal is not to authenticate these plaintext frames early.

The goal is simply:

> Frames that cannot possibly affect the current state should not consume scarce control-queue entries.

---

## A5. Fragmented-Message Admission and Allocation Failure

### Protocol rule

The protocol maximum remains:

```text
255 × 240 = 61,200 bytes
```

NMNW should continue allowing a logical message of that size.

There should not be a smaller universal protocol maximum merely because some devices cannot allocate 61 KB.

---

### Device-level receive capability

Each implementation may define how large a message it is willing or able to receive.

Examples:

```text
Gateway:
61,200 bytes

Large S3 device:
32 KiB

Small C3 leaf:
4 KiB
```

This is an implementation/resource limit, not a change to the wire protocol's maximum.

---

### Current concern

Reassembly currently grows a `std::vector` incrementally using fragment inserts.

This creates two problems:

1. repeated reallocations can increase heap fragmentation;
2. allocation failure is not represented explicitly at the NMNW protocol level.

---

### Proposed solution

The receiver should perform message admission when the first fragment arrives.

It knows:

```text
totalFrames
frame size
local accepted message capacity
```

It can therefore determine whether the message is acceptable before consuming the complete transmission.

---

### Allocation failure

If the required reservation cannot be made:

```text
fail the logical message
```

Do not continue partially assembling it.

Release the reassembly state immediately.

---

### Protocol-level rejection

The rejection must be associated with the logical `messageId`.

Example:

```text
MESSAGE #42
frame 0/100
    ↓
receiver determines message exceeds local capacity
    ↓
ERROR #42: MESSAGE_TOO_LARGE
```

or:

```text
MESSAGE #42
frame 0/100
    ↓
allocation fails
    ↓
ERROR #42: NO_MEMORY
```

The sender should then stop transmitting the remaining fragments belonging to `messageId = 42`.

---

### One logical message → one terminal result

The desired semantics are:

```text
one messageId
    ↓
one terminal ACK
or
one terminal ERROR
```

Not one ACK per fragment.

This keeps fragmentation an internal transport concern.

---

# 5. Bucket B — Acknowledge and Revisit Later

## B1. Plaintext Pre-Auth ERROR Influence

### Concern

Before the LMK is installed and verified, some control traffic remains plaintext.

An attacker capable of spoofing the gateway MAC may send a syntactically valid `ERROR`.

For example:

```text
ERROR AUTH_FAILED
```

while the client is authenticating.

This cannot establish a fake authenticated session.

It can, however, cause the client to abandon the current handshake or enter a retry/backoff path.

### Classification

Availability weakness only.

No:

- PSK compromise;
- session compromise;
- message injection;
- authenticated gateway impersonation.

### Current decision

Acknowledge and revisit later.

When revisited, plaintext pre-auth errors should be treated as advisory rather than authoritative.

They should not cause a stronger state transition than an ordinary failed handshake.

---

## B2. Duplicate Logical MESSAGE / Idempotency

### Concern

There is a conceptual difference between:

```text
cryptographic packet replay
```

and:

```text
application-level retry of a logical message
```

For example:

```text
MESSAGE #42 completes
→ application handles it
→ ACK is lost

sender retries MESSAGE #42
→ receiver could handle it again
```

For state updates this may be harmless.

For actions/events it may matter.

Examples:

```text
relay pulse
IR send
door command
one-shot event
```

### Important distinction

This is **not** an ESP-NOW/CCMP replay vulnerability.

CCMP prevents replaying the same encrypted RF frame.

This concern is about legitimate application retransmission semantics.

### Current decision

Acknowledge for later.

Potential future design:

```text
recentlyCompleted[session][messageId]
```

so that a duplicate completed logical message can be ACKed again without dispatching it twice.

This should be considered alongside ManagedEvent/action semantics rather than treated as a cryptographic transport flaw.

---

## B3. Fake BEACON Steering

### Concern

A BEACON is intentionally unauthenticated.

A nearby attacker may broadcast a valid-looking gateway BEACON while a client is searching.

The client may temporarily select the bogus gateway and attempt to authenticate.

### What the attacker gains

Only temporary discovery influence.

The attacker cannot complete authentication without the PSK.

The flow becomes:

```text
fake BEACON
    ↓
client chooses bogus gateway
    ↓
handshake begins
    ↓
attacker cannot prove PSK
    ↓
handshake fails
    ↓
client returns to searching
```

### Classification

Availability/discovery interference only.

This is close in nature to other local radio interference techniques and does not create a trust failure.

### Current decision

Acknowledge.

No immediate protocol change required.

---

# 6. Bucket C — Already Solved

## C1. Same-Session Replay

### Concern considered

Could an attacker capture an encrypted ESP-NOW frame and retransmit it during the same live session?

### Resolution

ESP-NOW encrypted peers use CCMP.

CCMP provides replay protection using packet-number/nonce state.

A previously accepted encrypted frame should therefore be rejected by the ESP-NOW/CCMP layer before it reaches NMNW.

### Status

Solved by ESP-NOW encryption / CCMP.

NMNW should not implement a redundant cryptographic replay mechanism for this.

---

## C2. Source-MAC Spoofing of Authenticated Traffic

### Concern considered

Could an attacker simply spoof a legitimate device's MAC address and send:

```text
MESSAGE
PING
DISCONNECT
SUBSCRIBE
etc.
```

as that device?

### Resolution

The MAC address alone is not the session credential.

Established peer traffic must authenticate under the session LMK.

A forged frame with the correct source MAC but without the LMK fails ESP-NOW encryption/authentication.

### Status

Solved by LMK + CCMP.

---

## C3. Cross-Session Replay

### Concern considered

Could traffic captured from an old authenticated session be replayed after the device reconnects?

### Resolution

Each authenticated handshake derives a new session LMK using fresh handshake nonces.

Therefore:

```text
old encrypted frame
    ↓
new session LMK
    ↓
frame cannot authenticate
```

### Status

Solved by fresh per-session LMK.

---

## C4. Handshake Replay

The handshake itself is plaintext, so replay was considered separately.

### Replayed CONNECT

The gateway generates a fresh `gatewayNonce`.

An old AUTH proof therefore does not match the new handshake context.

---

### Replayed AUTH

The AUTH proof covers:

```text
client MAC
gateway MAC
clientNonce
gatewayNonce
```

An old proof is invalid under fresh nonce values.

---

### Replayed CHALLENGE

The client correlates CHALLENGE with its current CONNECT/message state.

Even if an old challenge were replayed at the right time, the resulting client AUTH would combine:

```text
current clientNonce
+
replayed gatewayNonce
```

while the real gateway has a different current handshake context.

Authentication therefore fails.

---

### Replayed CONNACK

A stale CONNACK cannot finish authentication by itself.

After receiving CONNACK, the client derives and installs the LMK from its current handshake context and then requires:

```text
encrypted PING
↔
encrypted PONG
```

before entering CONNECTED.

A stale CONNACK cannot produce a valid encrypted securing exchange.

### Status

Solved by:

```text
fresh nonces
+
HMAC authentication
+
encrypted securing step
```

---

## C5. Fake Gateway Authentication

### Concern considered

Because BEACON, CHALLENGE, and CONNACK are plaintext, could an attacker impersonate the gateway?

### Resolution

An attacker may imitate the plaintext portion:

```text
BEACON
CHALLENGE
CONNACK
```

but cannot derive the same LMK without the PSK.

The client does not consider itself CONNECTED until it receives a valid encrypted PONG using the derived LMK.

Therefore a fake gateway cannot complete the authentication sequence.

### Status

Solved by PSK-derived LMK + encrypted PING/PONG confirmation.

---

## C6. CID / messageId Reuse Across Sessions

### Concern considered

Both CID and `messageId` are 16-bit values and eventually wrap.

Could an old packet become valid again after identifier reuse?

### Resolution

Identifier reuse across authenticated sessions is protected by the LMK.

Even if a future session happens to reuse:

```text
same MAC
same CID
same messageId
```

an old encrypted packet was produced under the old session LMK.

It cannot authenticate under the new LMK.

### Status

Solved by per-session LMK separation.

No wider CID/messageId is required for this security property.

---

# 7. Previously Identified CONNECT Session-Suspension Issue

A previous implementation allowed an unauthenticated CONNECT from a MAC with an existing authenticated session to:

```text
turn the ESP-NOW peer plaintext
+
suspend the authenticated session
```

while the new handshake was pending.

Because CONNECT itself proves nothing, an attacker capable of spoofing the legitimate MAC could repeatedly force that state and cause targeted denial of service.

This issue has already been fixed in the current local implementation but was not yet pushed at the time of this review.

It should therefore not be considered an open vulnerability.

The intended invariant is:

> An unauthenticated CONNECT must not be able to destroy or meaningfully downgrade an existing authenticated session.

---

# 8. Resulting Security Model

After the fixes above, the expected trust progression is:

```text
BEACON
    │
    │ untrusted discovery
    ▼

CONNECT / CHALLENGE
    │
    │ untrusted handshake setup
    ▼

AUTH
    │
    │ HMAC proves possession of PSK
    ▼

CONNACK
    │
    │ session parameters supplied
    ▼

LMK installed
    │
    ▼

encrypted PING/PONG
    │
    │ proves both sides derived same session
    ▼

CONNECTED
    │
    │ CCMP + LMK protected
    ▼

MESSAGE / SUBSCRIBE / PING / etc.
```

The security boundary is therefore intentionally:

```text
before successful AUTH + encrypted securing:
    never trusted

after encrypted securing:
    authenticated session
```

---

# 9. Fix-Now Summary

The immediate implementation work is:

```text
1. Failed-auth exponential backoff
   - per MAC
   - bounded
   - jittered
   - reset only after successful secure session
   - never let unauthenticated client-side events grow the trusted penalty

2. Pending-handshake admission
   - hard maximum
   - one slot per MAC
   - short absolute timeout
   - repeated CONNECT cannot extend slot forever
   - integrate with per-MAC failed-auth backoff

3. Early malformed-frame rejection
   - reject obviously invalid packets before gateway RX queue/task processing

4. Client control-queue prefilter
   - sender/state/messageId/CID checks before scarce queue capacity is consumed

5. Fragmented-message admission
   - protocol maximum remains 61,200 bytes
   - device may advertise/enforce a smaller local receive limit
   - reserve/admit explicitly
   - explicit allocation-failure handling
   - message-level ERROR on rejection
   - sender aborts remaining fragments
   - exactly one terminal ACK or ERROR per logical message
```

---

# 10. Acknowledge-for-Later Summary

```text
1. Plaintext pre-auth ERROR can influence handshake availability.
2. Logical MESSAGE retries may eventually need idempotency/deduplication.
3. Fake BEACONs may temporarily steer discovery but cannot authenticate.
```

None currently represents an authentication or confidentiality break.

---

# 11. Already-Solved Summary

```text
Same-session encrypted replay
    → CCMP

Source-MAC spoofing of authenticated traffic
    → LMK + CCMP

Cross-session replay
    → fresh session LMK

Handshake replay
    → fresh nonces + HMAC + securing exchange

Fake gateway authentication
    → cannot derive LMK / cannot complete encrypted PING-PONG

CID/messageId reuse across sessions
    → old traffic encrypted under old LMK
```

---

# Conclusion

The current NMNW ESP-NOW security architecture has a useful separation between:

```text
discovery
authentication
session establishment
encrypted session traffic
```

The core authentication design does not currently show an obvious path for a nearby unauthenticated attacker to impersonate a legitimate client or gateway when the PSK remains secure.

The remaining immediate work is mostly about making unauthenticated traffic cheap to reject and keeping resource use strictly bounded:

```text
rate-limit authentication work
bound pending state
discard malformed traffic early
protect small queues
make large-message admission explicit
```

The cryptographic replay and identity concerns examined in this review are already largely delegated to the combination of:

```text
fresh nonces
HMAC
fresh session LMKs
ESP-NOW CCMP
encrypted session confirmation
```

which is the correct layer for those protections.
