#pragma once
#include <Arduino.h>

namespace NightMare {

// Bounded shell-like tokenizer. Quotes group words; backslash escapes the next
// character. No expansion or device-specific interpretation happens here.
struct ParsedCommand {
    static constexpr uint8_t MAX_WORDS = 12;
    String words[MAX_WORDS];
    uint8_t count = 0;
    String tail(uint8_t first) const;
};

class CommandParser {
public:
    static bool parse(const String& line, ParsedCommand& parsed, String& error);
};

} // namespace NightMare
