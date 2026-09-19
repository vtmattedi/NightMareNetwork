#include <NightMare/Features.h>
#if NIGHTMARE_ENABLE_CONSOLE
#include <NightMare/Network/Console.h>

namespace NightMare {

bool Console::execute(const String& line) {
    return run(line).ok;
}

CommandResult Console::run(const String& line, CommandContext context) {
    return _router.execute(line, context);
}

void Console::tick(Stream& input) {
    while (input.available()) {
        char character = static_cast<char>(input.read());
        if (character == '\n') {
            if (!_discarding) {
                CommandResult result = run(_line, {CommandSource::Serial, SettingsAccess::User});
                input.println(result.text);
            } else input.println("error: command too long");
            _line = "";
            _discarding = false;
        } else if (character != '\r' && !_discarding) {
            if (_line.length() < 256) _line += character;
            else { _line = ""; _discarding = true; }
        }
    }
    _router.tick();
}

} // namespace NightMare
#endif
