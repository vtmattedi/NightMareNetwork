# ESP-NOW connection protocol

Status: **pre-v1**. NM protocol version `0`. No compatibility with the earlier
(unversioned-session) ESP-NOW frames: an old device and a new gateway ignore
each other.

This is the contract between a NightMareNetwork device (`Network/EspNow/`) and
the Nightmare Gateway. The wire definitions live in
`Network/EspNow/NightMareEspNow/Frame.*` and the handshake crypto in
`NightMareEspNow/Auth.*`. The gateway carries byte-identical copies of those
four files; change them together.

Scope: connection, authentication, session identity and link encryption. What
travels inside a session (topics, retained flag, last will) is unchanged, and
`NightMare::Message` knows nothing about sessions.

## Frame header

10 bytes, packed, little-endian:

| Offset | Field         | Type       | Meaning |
|-------:|---------------|------------|---------|
| 0      | `messageId`   | `uint16_t` | Request/response correlation. Fragments of one MESSAGE share it. |
| 2      | `version`     | `uint8_t`  | High nibble: ESP-NOW framing version. Low nibble: NM protocol version. |
| 3      | `type`        | `uint8_t`  | `FrameType` |
| 4      | `cid`         | `uint16_t` | Session id. `0` = no session. |
| 6      | `frameIndex`  | `uint8_t`  | `0..totalFrames-1` |
| 7      | `totalFrames` | `uint8_t`  | `1` except for a fragmented MESSAGE (up to 255, about 61 KB at V1). |
| 8      | `length`      | `uint16_t` | Data bytes after the header. |

**Version byte.** `makeVersion(espNow, nm) = (espNow << 4) | nm`.

- ESP-NOW framing (`EspNowFrameVersion`): `V1 = 0` is the 250-byte ESP-NOW
  payload (240 data bytes per frame). `V2 = 1` is the 1470-byte payload. V2 is
  reserved as a capability only: nothing sends it and it is rejected on
  receipt.
- NM protocol: `0` is this pre-v1 contract. It is not "v1" and stays `0` until
  an explicit freeze.

## Frame types

| Value | Type        | Direction | cid | Data | Encrypted |
|------:|-------------|-----------|-----|------|-----------|
| 0  | BEACON      | gateway → broadcast | 0 | `[count][EspNowFrameVersion…]` | no |
| 1  | CONNECT     | client → broadcast  | 0 | `ConnectPayload` (10) | no |
| 2  | CHALLENGE   | gateway → client    | 0 | `ChallengePayload` (8) | no |
| 3  | AUTH        | client → gateway    | 0 | `AuthPayload` (16) | no |
| 4  | CONNACK     | gateway → client    | new cid | `ConnAckPayload` (6) | no |
| 5  | DISCONNECT  | client → gateway    | cid | — | yes |
| 6  | PING        | client → gateway    | cid | — | yes |
| 7  | PONG        | gateway → client    | cid | — | yes |
| 8  | SUBSCRIBE   | client → gateway    | cid | topic filter | yes |
| 9  | UNSUBSCRIBE | client → gateway    | cid | topic filter | yes |
| 10 | MESSAGE     | both                | cid | `[retained:1][reserved:1][topicLen:6][topic][payload]` | yes |
| 11 | LAST_WILL   | client → gateway    | cid | same encoding as MESSAGE | yes |
| 12 | ACK         | gateway → client    | cid | — | yes |
| 13 | ERROR       | gateway → client    | any | `[ErrorCode]` (1) | if a session key is installed |

Replies echo the request's `messageId`: CHALLENGE echoes CONNECT, CONNACK
echoes AUTH, PONG echoes PING, and ACK/ERROR echo the frame they answer.

**Validation.** Every packet is checked before any payload is read
(`validateFrame()`):

- at least a header, and `length` equal to the bytes that follow it;
- V1 framing and NM protocol `0`;
- a known type;
- `totalFrames ≠ 0`, `frameIndex < totalFrames`, and only MESSAGE may be
  fragmented;
- the cid rule for the type: handshake types carry `0`, CONNACK and session
  types carry a non-zero cid, and ERROR may carry either;
- exact payload size for fixed-size types.

Anything else is dropped. The gateway answers a bad frame only when it comes
from a MAC that holds a session.

## Handshake

```
Gateway                              Client
   | -------- BEACON (broadcast) ------> |   every 5 s
   | <------- CONNECT (broadcast) ------ |   fresh clientNonce
   | -------- CHALLENGE ---------------> |   fresh gatewayNonce
   | <------- AUTH --------------------- |   proof
   | -------- CONNACK(cid) ------------> |   last plaintext frame
   |     both derive + install the LMK    |
   | <======= PING(cid) encrypted ====== |
   | ======== PONG(cid) encrypted ======> |
   |              CONNECTED               |
```

The client always broadcasts CONNECT. This does two jobs:

- It is the discovery probe: a CHALLENGE reply reveals the gateway's address.
- It reaches a gateway that still holds an encrypted peer for this MAC from a
  session the client has lost, for example because the client rebooted.

**An existing session survives the handshake; only a verified AUTH replaces
it.** A CONNECT proves nothing — anyone in radio range can send one — so it
never costs a session anything. When the sender MAC already has a session, the
gateway:

1. keeps the session, with its cid, subscriptions and last will;
2. turns the peer back to plaintext, because the device asking for a handshake
   cannot hold the current session's key, and **suspends** the session for the
   duration: nothing is delivered to it, and nothing claiming to be it is
   accepted (silently — an error here would tell the real device to tear down a
   session it is about to get back);
3. runs CHALLENGE and AUTH in plaintext.

Then one of two things happens:

- **AUTH verifies.** The sender holds the network key, so it is the device.
  Only now is the old session dropped, without firing its last will, and
  replaced by a new one with a fresh cid. The client resyncs its subscriptions
  and will. Replacing needs no room in the session table, so a device can
  always reconnect even when the gateway is full.
- **AUTH fails, or the handshake times out (5 s).** The session resumes exactly
  as it was, its key put back on the peer. Nothing was lost.

- *Benefit:* a client that rebooted and lost its cid and LMK recovers as soon
  as it proves itself, without waiting out the session timeout.
- *Cost:* a CONNECT that spoofs a device's MAC pauses that device's delivery
  for up to 5 s, and the device's own traffic is dropped meanwhile. Repeated,
  it is a denial of service against delivery, but the session itself is never
  lost and the device does not notice (5 s is well inside its heartbeat
  tolerance).

No reconnect proof or session resumption guards against the pause before v1.

**Auth proof and session key.** Both are HMAC-SHA256 keyed with the network
PSK and truncated to 16 bytes. They are computed over:

```
label | clientMac(6) | gatewayMac(6) | clientNonce(8, LE) | gatewayNonce(8, LE)
```

- `label = "NM-AUTH"` gives the proof sent in AUTH. The gateway compares it in
  constant time.
- `label = "NM-LMK"` gives the ESP-NOW peer LMK. It is never sent or stored, and
  a new one is derived for every handshake.
- The MACs are the ones ESP-NOW reports as the sender, never values a frame
  claims.
- Nonces come from the hardware RNG and are fresh for each attempt.

**PMK.** Both ends set the ESP-NOW primary key right after `esp_now_init()`,
before any encrypted peer exists, instead of relying on Espressif's default:

```
PMK = first16(HMAC-SHA256(PSK, "NM-PMK"))
```

It is the same on every node of the network and is wiped after
`esp_now_set_pmk()`. The three labels `NM-AUTH`, `NM-LMK` and `NM-PMK` keep the
proof and the two keys independent of each other.

**Encryption.** After CONNACK, both ends switch the peer to `encrypt = true`
with the LMK (`esp_now_mod_peer`). The gateway sends CONNACK first, then
installs the key. The session becomes CONNECTED on each side only after an
encrypted PING and PONG succeed. That exchange is what proves both ends
installed the same key.

**Session rules on the gateway.**

- A MAC with no session and no pending handshake may send only CONNECT.
  Nothing else creates any state.
- While a handshake is pending, only CONNECT and AUTH are admitted. Pending
  handshakes time out after 5 s, and at most 3 run at once.
- Session traffic needs both the sender MAC of the session and its cid.
  - A MAC that holds a session here but sends another cid gets
    `ERROR INVALID_SESSION` (cid 0).
  - A MAC with no session is ignored, whatever cid it sends. The gateway never
    adds a peer just to answer it.
  - A valid cid from any other MAC is ignored.
- Fragments are reassembled per sender MAC + cid + messageId. A messageId reused
  under a new session never joins fragments left over from the old one.
- Before CONNECTED (SECURING), only PING and DISCONNECT are admitted. Anything
  else gets `ERROR NOT_CONNECTED`. A session that has not secured after 5 s is
  dropped.
- A session suspended for a handshake accepts nothing at all and is answered
  with nothing. It cannot time out from silence either, since it is not allowed
  to speak; the 5 s handshake timeout decides its fate.
- The gateway allocates cids: non-zero, unique among live sessions, never
  persisted. A gateway reboot invalidates every cid.

## Keeping the session

The heartbeat interval is the gateway's and arrives in CONNACK (15 s today).
The client sends a PING each interval. It declares the session lost after
`Settings::missedBeforeLost` (default 3) unanswered PINGs. It then drops the
session (cid 0, plaintext peer), forgets the gateway and searches again.

The gateway drops a CONNECTED session after 4 heartbeat intervals of silence
and publishes its last will. A DISCONNECT, or a new CONNECT from the same MAC,
ends a session without the will.

On `ERROR INVALID_SESSION` or `NOT_CONNECTED` from its gateway, a client in a
session starts a new handshake at once. The gateway sends these only when it
holds a session for that MAC.

**Gateway reboot.** A rebooted gateway has no sessions and no peers. It ignores
the client's session traffic and sends no error. It could not decrypt that
traffic anyway, since it no longer holds the key. The client recovers through
its heartbeat: after `missedBeforeLost` unanswered PINGs (about 45 s with a
15 s heartbeat), it drops the session, rediscovers the gateway and runs a full
new handshake.

## Resync

Subscriptions and the last will are kept by `EspNowClient`. When a new session
reaches CONNECTED, the client re-sends them automatically, before any
application traffic. Nothing about this is visible to the application, and
none of it rides in CONNECT.

## Errors

| Code | Name | Sent when |
|-----:|------|-----------|
| 1 | AUTH_FAILED | The AUTH proof does not verify (different PSK). The client backs off 10 s. |
| 2 | UNSUPPORTED_VERSION | A session peer sent a framing or protocol version the gateway does not run. |
| 3 | INVALID_SESSION | The cid does not match the sender's session. |
| 4 | INVALID_FRAME | A session peer sent a frame that failed validation. |
| 5 | NOT_CONNECTED | Session traffic arrived before the session was secured. |
| 6 | REJECTED | A well-formed SUBSCRIBE, UNSUBSCRIBE or LAST_WILL was refused. |

Error payloads never carry secret-derived material.

## Configuration

- **Network key.** 16 to 64 bytes, the same on the gateway and every device.
  - Device: `NM_ESPNOW_PSK` in the project's `creds.h` (gitignored). A build
    with `NM_NETWORK_ESPNOW` fails without it.
  - Gateway: the application passes the key in
    `NightMareGatewayConfig::espnowConfig` to `start_nightmare_gateway()`.
    Today the gateway's `main` reads it from `NM_ESPNOW_PSK` in its `creds.h`.
    The gateway copies the key, hands it to `espBroker_init(psk, length)` (which
    keeps its own copy), and wipes the intermediate one.
- **Gateway peer limit.** `CONFIG_ESP_WIFI_ESPNOW_MAX_ENCRYPT_NUM=16`, one
  encrypted peer per session. The ESP-NOW peer table (20) holds 16 sessions, 3
  pending handshakes and the broadcast peer.

## Known limits

- **Plaintext injection is not detectable.** ESP-IDF's receive callback does
  not say whether a frame arrived encrypted. A plaintext frame that spoofs a
  session's MAC and its cid may be delivered. The cid is visible in the
  plaintext CONNACK and is not a credential. The protection that holds is on
  the other side: session traffic, including everything the gateway relays to
  the device, is encrypted, and only PSK holders can open a session.
- **A spoofed CONNECT can pause a session.** A CONNECT is unauthenticated by
  design, and it suspends any session held by its sender MAC until the
  handshake is proven or times out (5 s). It can no longer end one: that takes
  a verified AUTH. Repeated, it withholds delivery, much as jamming would.
- **V2 framing** is reserved but has no runtime.
- **SUBSCRIBE is not retried.** A SUBSCRIBE lost on the air after a resync is
  not re-sent until the next session.

## Tests

The gateway's on-device suite covers frame validation, the crypto (including
known-answer vectors computed independently) and the session table:
`pio test -e esp32-s3-devkitc1-n16r8 -f test_espnow_protocol` in the gateway
project.

The radio-level behaviour needs a gateway and a device. Checklist:

1. **Handshake.** A fresh device connects. The device log shows
   `authenticated as session N, securing` and then `connected ... session N`.
   The gateway shows `Session N (...) secured and connected`.
2. **Wrong PSK.** Change the device's `NM_ESPNOW_PSK`. The gateway logs
   `AUTH ... failed`, the device logs `NM_ESPNOW_PSK differs` and backs off, and
   no session appears in the gateway's device list.
3. **Resync.** After connecting, the gateway logs every subscription and the
   last will for the new session without any application action.
4. **Client reboot.** Reset the device. The gateway logs
   `session N paused until it is proven`, then `proved itself; session N
   replaced`, and the device reconnects within seconds with a new cid.
5. **Spoofed CONNECT.** Send a CONNECT with a connected device's MAC and a
   wrong key (or let one time out). The gateway logs the pause, then
   `session N resumes`, and the device carries on with the same cid and its
   subscriptions intact.
5. **Gateway reboot.** Reset the gateway. It sends the device no error. After
   about 45 s of missed heartbeats the device logs
   `gateway silent ... searching again`, then does a full new handshake with a
   new cid.
6. **Lost device.** Power the device off. After about 60 s the gateway publishes
   its last will.
7. **Clean stop.** `EspNowClient::end()` sends DISCONNECT. The gateway logs
   `disconnected` and no last will goes out.
