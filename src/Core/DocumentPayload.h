#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

/// @brief Which encoding a document is serialized into. Deliberately separate
/// from ManifestFormat and HardwareFormat: those say what a particular document
/// is published as, this says only how the bytes are produced.
enum class DocumentEncoding : uint8_t
{
    JSON,
    MSGPACK
};

/// @brief Why a payload could not be produced whole. Reported rather than
/// folded into one boolean because the three causes call for different
/// responses: an incomplete document means the heap was short while it was
/// being built, a failed reservation means the heap cannot offer one
/// contiguous block that size right now, and a short write means the
/// destination accepted fewer bytes than it was given. The first two pass, the
/// third does not.
enum class PayloadResult : uint8_t
{
    Complete,
    DocumentIncomplete,
    Empty,
    NoContiguousBlock,
    ShortWrite
};

const char *describePayloadResult(PayloadResult result);

/// @brief Serialize a document whole, or produce nothing at all.
///
/// Anything but Complete leaves `out` empty, and callers publish only on
/// Complete. Every document this guards is published retained, and a retained
/// payload that is short does not fail once and get retried: it sits on the
/// broker being read as the truth by everything that subscribes, until
/// something replaces it. Publishing nothing leaves the last good version in
/// place, which is always the better answer.
///
/// There are three ways a payload can come out short, and the return of
/// serializeJson() reports none of them:
///
///  * An allocation failed while the document was being built. ArduinoJson 7
///    has no fixed capacity, so nothing overflows a buffer -- but an
///    allocation can still fail, and the value is then dropped silently.
///    doc.overflowed() is what says so; the serialized output looks fine.
///  * The destination String could not grow. Its writer's block write returns
///    the count it was asked for whether or not those characters landed, so a
///    short write is invisible in the return value.
///  * The MessagePack contained a zero byte. ArduinoJson's String writer
///    NUL-terminates its buffer and appends it with String::concat(const
///    char *), which measures with strlen, so the payload is cut at the first
///    zero. MessagePack writes the integer 0 as the byte 0x00 and enums
///    encoded by value start at 0, so for a compact manifest -- whose first
///    resource begins with kind VALUE, which is 0 -- this is not an edge case
///    but the normal case.
///
/// Both encodings are therefore written through a sink that appends with an
/// explicit length and returns what it actually stored. The String is reserved
/// to the measured size first, so the payload costs exactly one contiguous
/// block: the one the finished payload has to occupy anyway, because that is
/// what gets handed to the publisher.
///
/// Measuring costs a second walk of the document and no allocation, which is
/// the price of the guarantee.
PayloadResult serializeWholeDocument(const JsonDocument &doc, DocumentEncoding encoding,
                                     String &out);
