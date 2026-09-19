#include <NightMare/Network/Console.h>

namespace NightMare {

bool Console::execute(const String& line) {
    String command = line;
    command.trim();
    int separator = command.indexOf(' ');
    String id = separator < 0 ? command : command.substring(0, separator);
    String argument = separator < 0 ? "" : command.substring(separator + 1);
    argument.trim();
    return id.length() && _manager.invokeLocal(id, argument);
}

void Console::tick(Stream& input) {
    while (input.available()) {
        char character = static_cast<char>(input.read());
        if (character == '\n') {
            if (!_discarding) execute(_line);
            _line = "";
            _discarding = false;
        } else if (character != '\r' && !_discarding) {
            if (_line.length() < 128) _line += character;
            else { _line = ""; _discarding = true; }
        }
    }
}

} // namespace NightMare
