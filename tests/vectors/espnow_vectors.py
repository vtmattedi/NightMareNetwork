#!/usr/bin/env python3
"""Reference implementation and known-answer vectors for the ESP-NOW handshake
(src/Network/EspNow/NightMareEspNow/Auth.*, docs/modules/espnow-protocol.md).

Run `python tests/vectors/espnow_vectors.py` to print the vectors as C++ constants.
Run with `--check` to verify tests/pio_smoke_core/src/EspNowVectors.h holds exactly these.
The key below is a public test value, not a real credential.
"""
import hashlib, hmac, struct, sys, re, pathlib

KEY = b"NM-TEST-KEY-0123456789ABCDEF-xyz"          # 32 bytes, test only
CLIENT_MAC = bytes.fromhex("246F28AABB01")
GATEWAY_MAC = bytes.fromhex("246F28CCDD02")
CLIENT_NONCE = 0x1122334455667788
GATEWAY_NONCE = 0x99AABBCCDDEEFF00


def ns_field(ns: bytes) -> bytes:
    """Empty namespace (legacy, NM protocol 0): nothing. Otherwise len byte + ID."""
    return b"" if not ns else bytes([len(ns)]) + ns


def first16(label: bytes, body: bytes) -> bytes:
    return hmac.new(KEY, label + body, hashlib.sha256).digest()[:16]


def session_body(ns, cmac=CLIENT_MAC, gmac=GATEWAY_MAC, cn=CLIENT_NONCE, gn=GATEWAY_NONCE):
    return ns_field(ns) + cmac + gmac + struct.pack("<QQ", cn, gn)


def proof(ns, **kw):
    return first16(b"NM-AUTH", session_body(ns, **kw))


def lmk(ns, **kw):
    return first16(b"NM-LMK", session_body(ns, **kw))


def pmk(ns):
    return first16(b"NM-PMK", ns_field(ns))


def ns_tag(ns: bytes) -> int:
    """First 4 digest bytes; the wire bytes are the digest bytes (little-endian uint32)."""
    return struct.unpack("<I", hashlib.sha256(ns).digest()[:4])[0]


def cpp(b: bytes) -> str:
    return ", ".join("0x%02X" % x for x in b)


def vectors():
    out = {}
    for name, ns in (("V0", b""), ("HOME", b"home"), ("TURING", b"turing")):
        out[name + "_PROOF"] = proof(ns)
        out[name + "_LMK"] = lmk(ns)
        out[name + "_PMK"] = pmk(ns)
    out["HOME_NSTAG"] = ns_tag(b"home")
    out["TURING_NSTAG"] = ns_tag(b"turing")
    return out


if __name__ == "__main__":
    v = vectors()
    text = ""
    for k, val in v.items():
        text += ("constexpr uint32_t %s = 0x%08XU;\n" % (k, val)) if isinstance(val, int) else \
                ("constexpr uint8_t %s[16] = {%s};\n" % (k, cpp(val)))
    if "--check" in sys.argv:
        main = (pathlib.Path(__file__).parent.parent / "pio_smoke_core/src/EspNowVectors.h").read_text()
        main = main.replace("\r\n", "\n")
        missing = [l for l in text.splitlines() if l not in main]
        print("OK" if not missing else "MISSING in EspNowVectors.h:\n" + "\n".join(missing))
        sys.exit(1 if missing else 0)
    print(text, end="")
