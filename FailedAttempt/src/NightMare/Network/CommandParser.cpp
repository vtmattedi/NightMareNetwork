#include <NightMare/Features.h>
#if NIGHTMARE_ENABLE_CONSOLE
#include <NightMare/Network/CommandParser.h>

namespace NightMare {

String ParsedCommand::tail(uint8_t first) const {
    String result;
    for (uint8_t i = first; i < count; ++i) {
        if (i != first) result += ' ';
        result += words[i];
    }
    return result;
}

bool CommandParser::parse(const String& line, ParsedCommand& parsed, String& error) {
    parsed = {};
    error = "";
    if (line.length() > 256) { error = "command too long"; return false; }
    String word;
    bool active = false, escaped = false;
    char quote = 0;
    for (size_t i = 0; i <= line.length(); ++i) {
        const char ch = i == line.length() ? ' ' : line[i];
        if (escaped) {
            if (i == line.length()) { error = "unterminated escape"; return false; }
            word += ch; escaped = false; active = true; continue;
        }
        if (ch == '\\' && i < line.length()) { escaped = true; active = true; continue; }
        if (quote) {
            if (ch == quote) quote = 0;
            else word += ch;
            continue;
        }
        if (ch == '"' || ch == '\'') { quote = ch; active = true; continue; }
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            if (active) {
                if (parsed.count == ParsedCommand::MAX_WORDS) { error = "too many arguments"; return false; }
                parsed.words[parsed.count++] = word;
                word = "";
                active = false;
            }
        } else { word += ch; active = true; }
    }
    if (quote) { error = "unterminated quote"; return false; }
    return true;
}

} // namespace NightMare
#endif
