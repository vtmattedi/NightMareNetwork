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

/// @brief Serialize a document whole, or produce nothing at all.
///
/// Returns false, with `out` left empty, whenever the payload would be less
/// than the entire document. Callers publish only on true. Every document this
/// guards is published retained, and a retained payload that is short does not
/// fail once and get retried: it sits on the broker being read as the truth by
/// everything that subscribes, until something replaces it. Publishing nothing
/// leaves the last good version in place, which is always the better answer.
///
/// There are three ways a payload can come out short, and the return of
/// serializeJson() reports none of them:
///
///  * An allocation failed while the document was being built. ArduinoJson 7
///    has no fixed capacity, so nothing overflows a buffer -- but an
///    allocation can still fail, and the value is then dropped silently.
///    doc.overflowed() is what says so; the serialized output looks fine.
///  * The destination String could not grow. The String writer's block write
///    returns the count it was asked for whether or not those characters
///    landed, so a short write is invisible in the return value.
///  * The MessagePack contained a zero byte. That writer NUL-terminates its
///    buffer and appends it with String::concat(const char *), which measures
///    with strlen, so the payload is cut at the first zero. MessagePack writes
///    the integer 0 as the byte 0x00 and enums encoded by value start at 0, so
///    for the compact manifest -- whose first resource begins with kind VALUE,
///    which is 0 -- this is not an edge case but the normal case. MessagePack
///    is therefore serialized through an explicitly sized buffer instead of
///    through that writer.
///
/// Measuring costs a second walk of the document and no allocation, which is
/// the price of the guarantee.
bool serializeWholeDocument(const JsonDocument &doc, DocumentEncoding encoding, String &out);
