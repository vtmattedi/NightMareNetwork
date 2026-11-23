#include "Auth.h"
#include <esp_random.h>
#include <mbedtls/md.h>
#include <string.h>

// Shared byte for byte by the gateway and the NightMareNetwork client.

namespace NightMare::EspNowAuth
{
    namespace
    {
        constexpr char AuthLabel[] = "NM-AUTH";
        constexpr char LmkLabel[] = "NM-LMK";
        constexpr char PmkLabel[] = "NM-PMK";
        constexpr size_t MaxLabelLength = 8;
        constexpr size_t MaxNsFieldLength = 1 + MaxNamespaceLength; // length byte + ID
        constexpr size_t Sha256Size = 32;

        void putLe64(uint8_t *out, uint64_t value)
        {
            for (size_t i = 0; i < 8; ++i)
                out[i] = static_cast<uint8_t>(value >> (8 * i));
        }

        // first `outLength` bytes of HMAC-SHA256(psk, message)
        bool hmacTruncated(const uint8_t *psk, size_t pskLength, const uint8_t *message,
                           size_t messageLength, uint8_t *out, size_t outLength)
        {
            memset(out, 0, outLength);
            if (psk == nullptr || pskLength == 0)
                return false;
            uint8_t digest[Sha256Size];
            const mbedtls_md_info_t *sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
            const bool ok = sha256 != nullptr &&
                            mbedtls_md_hmac(sha256, psk, pskLength, message, messageLength, digest) == 0;
            if (ok)
                memcpy(out, digest, outLength);
            wipe(digest, sizeof(digest));
            return ok;
        }

        // label, then (namespaced only) len(nsId) | nsId. Returns the bytes used.
        size_t putLabel(uint8_t *out, const char *label, const char *nsId, size_t nsIdLength)
        {
            const size_t labelLength = strlen(label);
            memcpy(out, label, labelLength);
            size_t used = labelLength;
            if (nsIdLength > 0)
            {
                out[used++] = static_cast<uint8_t>(nsIdLength);
                memcpy(out + used, nsId, nsIdLength);
                used += nsIdLength;
            }
            return used;
        }

        bool derive(const char *label, const uint8_t *psk, size_t pskLength,
                    const HandshakeContext &context, uint8_t *out, size_t outLength)
        {
            if (strlen(label) > MaxLabelLength || context.nsIdLength > MaxNamespaceLength)
            {
                memset(out, 0, outLength);
                return false;
            }

            uint8_t message[MaxLabelLength + MaxNsFieldLength + 2 * MacSize + 2 * 8];
            size_t used = putLabel(message, label, context.nsId, context.nsIdLength);
            memcpy(message + used, context.clientMac, MacSize);
            used += MacSize;
            memcpy(message + used, context.gatewayMac, MacSize);
            used += MacSize;
            putLe64(message + used, context.clientNonce);
            used += 8;
            putLe64(message + used, context.gatewayNonce);
            used += 8;

            const bool ok = hmacTruncated(psk, pskLength, message, used, out, outLength);
            wipe(message, sizeof(message));
            return ok;
        }
    }

    bool authProof(const uint8_t *psk, size_t pskLength, const HandshakeContext &context,
                   uint8_t out[ProofSize])
    {
        return derive(AuthLabel, psk, pskLength, context, out, ProofSize);
    }

    bool sessionLmk(const uint8_t *psk, size_t pskLength, const HandshakeContext &context,
                    uint8_t out[LmkSize])
    {
        return derive(LmkLabel, psk, pskLength, context, out, LmkSize);
    }

    bool setNamespace(HandshakeContext &context, const char *nsId, size_t nsIdLength)
    {
        context.nsIdLength = 0;
        memset(context.nsId, 0, sizeof(context.nsId));
        if (nsId == nullptr || nsIdLength == 0)
            return true;
        if (nsIdLength > MaxNamespaceLength)
            return false;
        memcpy(context.nsId, nsId, nsIdLength);
        context.nsIdLength = static_cast<uint8_t>(nsIdLength);
        return true;
    }

    bool networkPmk(const uint8_t *psk, size_t pskLength, uint8_t out[PmkSize])
    {
        return networkPmk(psk, pskLength, nullptr, 0, out);
    }

    bool networkPmk(const uint8_t *psk, size_t pskLength, const char *nsId, size_t nsIdLength,
                    uint8_t out[PmkSize])
    {
        if (nsId == nullptr)
            nsIdLength = 0;
        if (nsIdLength > MaxNamespaceLength)
        {
            memset(out, 0, PmkSize);
            return false;
        }
        uint8_t message[MaxLabelLength + MaxNsFieldLength];
        const size_t used = putLabel(message, PmkLabel, nsId, nsIdLength);
        return hmacTruncated(psk, pskLength, message, used, out, PmkSize);
    }

    uint32_t namespaceTag(const char *nsId, size_t nsIdLength)
    {
        if (nsId == nullptr || nsIdLength == 0)
            return 0;
        uint8_t digest[Sha256Size];
        const mbedtls_md_info_t *sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
        if (sha256 == nullptr ||
            mbedtls_md(sha256, reinterpret_cast<const uint8_t *>(nsId), nsIdLength, digest) != 0)
            return 0;
        uint32_t tag = 0;
        for (size_t i = 0; i < 4; ++i) // digest[0] is the low byte: wire bytes = digest bytes
            tag |= static_cast<uint32_t>(digest[i]) << (8 * i);
        return tag;
    }

    bool constantTimeEqual(const uint8_t *a, const uint8_t *b, size_t length)
    {
        uint8_t difference = 0;
        for (size_t i = 0; i < length; ++i)
            difference |= static_cast<uint8_t>(a[i] ^ b[i]);
        return difference == 0;
    }

    uint64_t freshNonce()
    {
        uint64_t nonce = 0;
        esp_fill_random(&nonce, sizeof(nonce));
        return nonce;
    }

    void wipe(void *data, size_t length)
    {
        volatile uint8_t *bytes = static_cast<volatile uint8_t *>(data);
        while (length-- > 0)
            *bytes++ = 0;
    }
}
