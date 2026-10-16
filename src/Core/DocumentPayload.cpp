#include "DocumentPayload.h"
#include <esp_heap_caps.h>

namespace
{
/// @brief ArduinoJson accepts this sink through its ordinary writer interface.
/// Its block write keeps MessagePack zero bytes intact.
class ByteSink
{
public:
    explicit ByteSink(std::string &out) : out_(out) {}

    size_t write(uint8_t value)
    {
        out_.push_back(static_cast<char>(value));
        return 1;
    }

    size_t write(const uint8_t *data, size_t size)
    {
        // The count, never a terminator: a MessagePack payload is allowed to
        // contain a zero byte and routinely does.
        out_.append(reinterpret_cast<const char *>(data), size);
        return size;
    }

private:
    std::string &out_;
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

const char *documentFormatName(DocumentFormat format)
{
    return format == DocumentFormat::MSGPACK ? "msgpack" : "json";
}

std::string resolveDocumentTopic(const std::string &deviceName, const std::string &documentRoot,
                            DocumentFormat format)
{
    std::string topic = deviceName;
    topic += '/';
    topic += documentRoot;
    topic += '/';
    topic += documentFormatName(format);
    return topic;
}

PayloadResult serializeWholeDocument(const JsonDocument &doc, DocumentFormat format,
                                     std::string &out)
{
    out = std::string();
    // Asked first: a document that lost a value while it was being built can
    // still serialize cleanly to a payload that is simply missing things.
    if (doc.overflowed())
        return PayloadResult::DocumentIncomplete;

    const bool packed = format == DocumentFormat::MSGPACK;
    const size_t expected = packed ? measureMsgPack(doc) : measureJson(doc);
    if (expected == 0)
        return PayloadResult::Empty;

    // One allocation, of exactly the size the finished payload needs. Reserving
    // up front also stops the sink reallocating as it appends, and it is the
    // honest place for this to fail: if the heap cannot offer this block now,
    // it could not have held the payload either.
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < expected + 1)
    {
        out = std::string();
        return PayloadResult::NoContiguousBlock;
    }
    out.reserve(expected);

    ByteSink sink(out);
    const size_t written = packed ? serializeMsgPack(doc, sink) : serializeJson(doc, sink);
    if (written != expected || out.length() != expected)
    {
        out = std::string();
        return PayloadResult::ShortWrite;
    }
    return PayloadResult::Complete;
}
