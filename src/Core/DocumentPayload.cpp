#include "DocumentPayload.h"

namespace
{
/// @brief Appends to a String by explicit length, and reports what it stored.
///
/// Deriving from Print is what makes ArduinoJson use it: PrintWriter.hpp
/// specializes Writer for anything derived from Print and forwards the real
/// return value, where the String writer reports success for a block it never
/// stored. Both properties matter here -- the length makes it binary-safe, and
/// the honest count is what lets the caller tell a whole payload from a
/// truncated one.
class StringSink : public Print
{
public:
    explicit StringSink(String &out) : out_(out) {}

    size_t write(uint8_t value) override
    {
        return out_.concat(static_cast<char>(value)) ? 1 : 0;
    }

    size_t write(const uint8_t *data, size_t size) override
    {
        // The count, never a terminator: a MessagePack payload is allowed to
        // contain a zero byte and routinely does.
        return out_.concat(reinterpret_cast<const char *>(data),
                           static_cast<unsigned int>(size))
                   ? size
                   : 0;
    }

private:
    String &out_;
};
}

const char *describePayloadResult(PayloadResult result)
{
    switch (result)
    {
    case PayloadResult::Complete:
        return "complete";
    case PayloadResult::DocumentIncomplete:
        return "the document lost a value to a failed allocation while it was built";
    case PayloadResult::Empty:
        return "the document measured zero bytes";
    case PayloadResult::NoContiguousBlock:
        return "no contiguous block that size was available";
    case PayloadResult::ShortWrite:
    default:
        return "the destination stored fewer bytes than it was given";
    }
}

PayloadResult serializeWholeDocument(const JsonDocument &doc, DocumentEncoding encoding,
                                     String &out)
{
    out = String();
    // Asked first: a document that lost a value while it was being built can
    // still serialize cleanly to a payload that is simply missing things.
    if (doc.overflowed())
        return PayloadResult::DocumentIncomplete;

    const bool packed = encoding == DocumentEncoding::MSGPACK;
    const size_t expected = packed ? measureMsgPack(doc) : measureJson(doc);
    if (expected == 0)
        return PayloadResult::Empty;

    // One allocation, of exactly the size the finished payload needs. Reserving
    // up front also stops the sink reallocating as it appends, and it is the
    // honest place for this to fail: if the heap cannot offer this block now,
    // it could not have held the payload either.
    if (!out.reserve(expected))
    {
        out = String();
        return PayloadResult::NoContiguousBlock;
    }

    StringSink sink(out);
    const size_t written = packed ? serializeMsgPack(doc, sink) : serializeJson(doc, sink);
    if (written != expected || out.length() != expected)
    {
        out = String();
        return PayloadResult::ShortWrite;
    }
    return PayloadResult::Complete;
}
