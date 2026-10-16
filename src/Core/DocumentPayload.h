#pragma once
#include <Core/Text.h>
#include <Core/PlatformClock.h>
#include <ArduinoJson.h>

/// @brief Which encoding a document is serialized into. Deliberately separate
/// from higher-level document selection: this says only how bytes are produced.
enum class DocumentFormat : uint8_t
{
    MSGPACK,
    JSON
};

const char *documentFormatName(DocumentFormat format);

/// @brief Build `<device>/<document-root>/<encoding>`.
/// Document roots are namespaces and never payload topics themselves.
std::string resolveDocumentTopic(const std::string &deviceName, const std::string &documentRoot,
                            DocumentFormat format);

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
/// ArduinoJson 7 can silently drop a value if a document allocation fails, so
/// overflowed() is checked before serialization. The output is measured and
/// reserved before writing. A byte-counted sink preserves embedded zero bytes
/// in MessagePack and reports how many bytes were written.
///
/// Measuring costs a second walk of the document and no allocation, which is
/// the price of the guarantee.
PayloadResult serializeWholeDocument(const JsonDocument &doc, DocumentFormat format,
                                     std::string &out);
