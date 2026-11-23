#pragma once
// Known-answer checks for the ESP-NOW handshake (NS-L3). The constants are generated
// by tests/vectors/espnow_vectors.py (an independent HMAC/hashlib implementation);
// `python tests/vectors/espnow_vectors.py --check` verifies they match that script.
// The gateway consumes the very same Auth.* / Frame.* files, so one run covers both.
// The key is a public test value, not a credential.

#include <Network/EspNow/NightMareEspNow/Auth.h>
#include <Network/EspNow/NightMareEspNow/Frame.h>

#include <string.h>

namespace EspNowVectors
{
namespace Auth = NightMare::EspNowAuth;
namespace Nm = NightMare;

constexpr char Key[] = "NM-TEST-KEY-0123456789ABCDEF-xyz";
constexpr size_t KeyLength = sizeof(Key) - 1;
constexpr uint8_t ClientMac[6] = {0x24, 0x6F, 0x28, 0xAA, 0xBB, 0x01};
constexpr uint8_t GatewayMac[6] = {0x24, 0x6F, 0x28, 0xCC, 0xDD, 0x02};
constexpr uint64_t ClientNonce = 0x1122334455667788ULL;
constexpr uint64_t GatewayNonce = 0x99AABBCCDDEEFF00ULL;

constexpr uint8_t V0_PROOF[16] = {0x8F, 0x40, 0x0B, 0xC7, 0x95, 0x6F, 0xC4, 0x75, 0x91, 0x7A, 0x04, 0x89, 0x86, 0x78, 0xB1, 0x29};
constexpr uint8_t V0_LMK[16] = {0xA3, 0x11, 0x45, 0xE6, 0x21, 0x01, 0xF7, 0x4A, 0xEF, 0x5C, 0x8E, 0x90, 0xC8, 0x98, 0x98, 0x86};
constexpr uint8_t V0_PMK[16] = {0xB1, 0xE4, 0x0D, 0x20, 0x0F, 0x31, 0xB7, 0x73, 0x0D, 0xC6, 0x46, 0x6D, 0x29, 0x81, 0x06, 0x68};
constexpr uint8_t HOME_PROOF[16] = {0x46, 0x4B, 0xBC, 0x8B, 0x2A, 0xF2, 0x2D, 0x60, 0x93, 0x65, 0x71, 0x5C, 0x33, 0x4E, 0x56, 0x6F};
constexpr uint8_t HOME_LMK[16] = {0x3A, 0x34, 0xBD, 0x7D, 0x4A, 0x39, 0x03, 0xA8, 0xF0, 0x27, 0x41, 0xB4, 0x30, 0x35, 0x2F, 0xBA};
constexpr uint8_t HOME_PMK[16] = {0x11, 0xF5, 0xD6, 0xE6, 0x58, 0x21, 0x73, 0x7A, 0xCC, 0x2E, 0xFD, 0xB7, 0x43, 0x5C, 0x0D, 0xAA};
constexpr uint8_t TURING_PROOF[16] = {0x06, 0x60, 0x77, 0x49, 0x76, 0x0B, 0x8C, 0x88, 0xE5, 0x21, 0x8C, 0xC9, 0xF3, 0x12, 0xA4, 0x81};
constexpr uint8_t TURING_LMK[16] = {0xA1, 0x9B, 0xDA, 0x2C, 0xA7, 0x11, 0xB3, 0x08, 0xAD, 0xA8, 0xE3, 0xFB, 0x99, 0x27, 0x21, 0x6F};
constexpr uint8_t TURING_PMK[16] = {0xB8, 0x0B, 0x19, 0x56, 0xA1, 0xBF, 0xA7, 0x2C, 0x1A, 0x53, 0x4C, 0xB5, 0xD3, 0x5C, 0x88, 0x26};
constexpr uint32_t HOME_NSTAG = 0x5840A14EU;
constexpr uint32_t TURING_NSTAG = 0xBE9B4EC8U;

inline Auth::HandshakeContext context(const char *ns)
{
    Auth::HandshakeContext c{};
    memcpy(c.clientMac, ClientMac, 6);
    memcpy(c.gatewayMac, GatewayMac, 6);
    c.clientNonce = ClientNonce;
    c.gatewayNonce = GatewayNonce;
    Auth::setNamespace(c, ns, strlen(ns));
    return c;
}

inline bool derivationsMatch(const char *ns, const uint8_t *proof, const uint8_t *lmk, const uint8_t *pmk)
{
    const Auth::HandshakeContext c = context(ns);
    uint8_t out[16];
    bool ok = Auth::authProof(reinterpret_cast<const uint8_t *>(Key), KeyLength, c, out) &&
              memcmp(out, proof, 16) == 0;
    ok = ok && Auth::sessionLmk(reinterpret_cast<const uint8_t *>(Key), KeyLength, c, out) &&
         memcmp(out, lmk, 16) == 0;
    ok = ok && Auth::networkPmk(reinterpret_cast<const uint8_t *>(Key), KeyLength, ns, strlen(ns), out) &&
         memcmp(out, pmk, 16) == 0;
    return ok;
}

inline bool framesBehave()
{
    const uint8_t previous = Nm::activeProtocolVersion();
    bool ok = true;
    uint8_t wire[Nm::MaxPacketSizeV1];

    // Protocol 0 (default): the 10-byte CONNECT, version byte 0x00.
    Nm::setActiveProtocolVersion(Nm::NM_PROTOCOL_VERSION);
    Nm::Frame f{};
    const Nm::ConnectPayload c0{ClientNonce, 0};
    ok = ok && Nm::makeFrame(f, Nm::FrameType::CONNECT, 1, 0, &c0, sizeof(c0)) && f.header.version == 0x00;
    memcpy(wire, &f, Nm::frameSize(f));
    ok = ok && Nm::validateFrame(wire, Nm::frameSize(f)) == Nm::FrameCheck::OK;
    const size_t v0Size = Nm::frameSize(f);

    // Protocol 1: the 20-byte CONNECT, version byte 0x01; a v0 frame is refused.
    Nm::setActiveProtocolVersion(Nm::NM_PROTOCOL_VERSION_NS);
    ok = ok && Nm::validateFrame(wire, v0Size) == Nm::FrameCheck::UNSUPPORTED_PROTOCOL;
    Nm::ConnectPayloadV1 c1{};
    c1.clientNonce = ClientNonce;
    c1.nsTag = HOME_NSTAG;
    memcpy(c1.clientMac, ClientMac, 6);
    ok = ok && Nm::makeFrame(f, Nm::FrameType::CONNECT, 1, 0, &c1, sizeof(c1)) && f.header.version == 0x01;
    memcpy(wire, &f, Nm::frameSize(f));
    ok = ok && Nm::frameSize(f) == Nm::FrameHeaderSize + 20 &&
         Nm::validateFrame(wire, Nm::frameSize(f)) == Nm::FrameCheck::OK;
    // nsTag travels as the SHA-256 digest bytes: 4E A1 40 58 for "home".
    ok = ok && wire[Nm::FrameHeaderSize + 10] == 0x4E && wire[Nm::FrameHeaderSize + 13] == 0x58;
    // A 10-byte CONNECT carrying v1 is the wrong size.
    ok = ok && Nm::makeFrame(f, Nm::FrameType::CONNECT, 1, 0, &c0, sizeof(c0));
    memcpy(wire, &f, Nm::frameSize(f));
    ok = ok && Nm::validateFrame(wire, Nm::frameSize(f)) == Nm::FrameCheck::BAD_PAYLOAD;

    ok = ok && !Nm::setActiveProtocolVersion(2) && Nm::activeProtocolVersion() == Nm::NM_PROTOCOL_VERSION_NS;
    Nm::setActiveProtocolVersion(previous);
    return ok;
}

inline bool allOk()
{
    bool ok = derivationsMatch("", V0_PROOF, V0_LMK, V0_PMK) &&
              derivationsMatch("home", HOME_PROOF, HOME_LMK, HOME_PMK) &&
              derivationsMatch("turing", TURING_PROOF, TURING_LMK, TURING_PMK);
    ok = ok && Auth::namespaceTag("home", 4) == HOME_NSTAG && Auth::namespaceTag("turing", 6) == TURING_NSTAG &&
         Auth::namespaceTag("", 0) == 0;
    // Isolation: a proof from one namespace is not another's, and the clientMac field is bound.
    uint8_t a[16], b[16];
    Auth::HandshakeContext home = context("home");
    Auth::HandshakeContext other = context("turing");
    ok = ok && Auth::authProof(reinterpret_cast<const uint8_t *>(Key), KeyLength, home, a) &&
         Auth::authProof(reinterpret_cast<const uint8_t *>(Key), KeyLength, other, b) && memcmp(a, b, 16) != 0;
    other = home;
    other.clientMac[5] ^= 1;
    ok = ok && Auth::authProof(reinterpret_cast<const uint8_t *>(Key), KeyLength, other, b) && memcmp(a, b, 16) != 0;
    // An ID over 16 bytes is refused, and leaves the legacy derivation.
    Auth::HandshakeContext tooLong = context("");
    ok = ok && !Auth::setNamespace(tooLong, "abcdefghijklmnopq", 17) && tooLong.nsIdLength == 0;
    return ok && framesBehave();
}
}
