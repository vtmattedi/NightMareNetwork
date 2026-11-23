#pragma once
#include <stdint.h>
#include <stddef.h>

// NightMare ESP-NOW session authentication. Shared byte for byte by the gateway
// and the NightMareNetwork client (the gateway consumes this very file through
// its nightmarenetwork submodule, so there is no second copy to keep in step).
//
// Every value is HMAC-SHA256 keyed with the namespace key (NM_ESPNOW_PSK),
// truncated to 16 bytes. Per session, over
//     label | [nsId] | clientMac(6) | gatewayMac(6) | clientNonce(8, LE) | gatewayNonce(8, LE)
// with label "NM-AUTH" for the proof sent in AUTH and "NM-LMK" for the ESP-NOW
// peer key. Per network, over the label "NM-PMK" and the namespace alone, for
// the ESP-NOW primary key (esp_now_set_pmk) instead of Espressif's default. The
// labels keep the three apart: the proof is on the wire, the keys never are.
//
// [nsId] is the namespace binding (NM protocol 1): one length byte, then that
// many ID bytes (1..16 of [a-z0-9-]). The length prefix makes the boundary
// between the ID and the fields after it unambiguous. With an EMPTY namespace
// (legacy, NM protocol 0) nothing at all is inserted, so the protocol 0 inputs
// are byte for byte what they always were.
//
// clientMac is the value CONNECT carries (protocol 1) or the radio sender
// address (protocol 0). The gateway currently requires the two to be equal.

namespace NightMare::EspNowAuth
{
    constexpr size_t MacSize = 6;
    constexpr size_t ProofSize = 16;
    constexpr size_t LmkSize = 16;
    constexpr size_t PmkSize = 16;
    // Both ends refuse a PSK outside these bounds. 64 is the HMAC-SHA256 block:
    // a longer key would only be hashed down first.
    constexpr size_t MinPskLength = 16;
    constexpr size_t MaxPskLength = 64;

    // Longest namespace ID (Core/NamespaceId.h: NamespaceId::MaxLength). Its own
    // constant so this file needs no other header.
    constexpr size_t MaxNamespaceLength = 16;

    struct HandshakeContext
    {
        uint8_t clientMac[MacSize];
        uint8_t gatewayMac[MacSize];
        uint64_t clientNonce;
        uint64_t gatewayNonce;
        // Namespace binding. nsIdLength 0 selects the legacy, protocol 0 derivation.
        uint8_t nsIdLength;
        char nsId[MaxNamespaceLength];
    };

    // Sets context.nsId. False (context left legacy) if the ID is longer than
    // MaxNamespaceLength. An empty or null ID selects the legacy derivation.
    bool setNamespace(HandshakeContext &context, const char *nsId, size_t nsIdLength);

    // False only if the HMAC could not be computed; `out` is then zeroed.
    bool authProof(const uint8_t *psk, size_t pskLength, const HandshakeContext &context,
                   uint8_t out[ProofSize]);
    bool sessionLmk(const uint8_t *psk, size_t pskLength, const HandshakeContext &context,
                    uint8_t out[LmkSize]);
    // Same on every node of the network; set once, right after esp_now_init().
    // The short form is the protocol 0 PMK. The namespaced form binds the ID:
    //     PMK = first16(HMAC-SHA256(key, "NM-PMK" | len(nsId) | nsId))
    // and equals the short form when nsIdLength is 0. The ESP-NOW PMK is
    // radio-global, so each side computes it once at init from its namespace.
    bool networkPmk(const uint8_t *psk, size_t pskLength, uint8_t out[PmkSize]);
    bool networkPmk(const uint8_t *psk, size_t pskLength, const char *nsId, size_t nsIdLength,
                    uint8_t out[PmkSize]);

    // CONNECT's nsTag: the first 4 bytes of SHA-256(nsId). On the wire these are
    // the digest bytes verbatim, which is the little-endian encoding of the
    // returned value (digest[0] is the least significant byte). A hint that lets
    // a gateway of another namespace stay quiet, not a security boundary. 0 for
    // an empty ID, which is never sent.
    uint32_t namespaceTag(const char *nsId, size_t nsIdLength);

    // Timing does not depend on where the inputs differ.
    bool constantTimeEqual(const uint8_t *a, const uint8_t *b, size_t length);

    // From the hardware RNG. Fresh per connection attempt, never reused.
    uint64_t freshNonce();

    // Zeroes key material in a way the compiler cannot drop.
    void wipe(void *data, size_t length);
}
