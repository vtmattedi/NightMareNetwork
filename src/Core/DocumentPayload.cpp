#include "DocumentPayload.h"

bool serializeWholeDocument(const JsonDocument &doc, DocumentEncoding encoding, String &out)
{
    out = String();
    // Asked first: a document that lost a value while it was being built can
    // still serialize cleanly to a payload that is simply missing things.
    if (doc.overflowed())
        return false;

    const size_t expected =
        encoding == DocumentEncoding::MSGPACK ? measureMsgPack(doc) : measureJson(doc);
    if (expected == 0)
        return false;

    if (encoding == DocumentEncoding::JSON)
    {
        // Text contains no zero byte, so the String writer is safe here and
        // only the length it actually produced needs checking.
        const size_t written = serializeJson(doc, out);
        if (written == expected && out.length() == expected)
            return true;
        out = String();
        return false;
    }

    // Exactly the measured size. This is the only contiguous allocation on the
    // path and it is as small as the document allows -- 375 bytes for a real
    // 20-resource manifest, against the 16KB that sizing by the protocol
    // ceiling would have asked for.
    char *buffer = (char *)malloc(expected);
    if (buffer == nullptr)
        return false;
    const size_t written = serializeMsgPack(doc, buffer, expected);
    // concat() with an explicit length, so the copy is bounded by the count
    // rather than by a terminator the payload is allowed to contain.
    const bool complete = written == expected &&
                          out.concat(buffer, (unsigned int)expected) &&
                          out.length() == expected;
    free(buffer);
    if (!complete)
        out = String();
    return complete;
}
