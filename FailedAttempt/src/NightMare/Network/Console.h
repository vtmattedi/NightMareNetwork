#pragma once
#include <NightMare/Network/CommandRouter.h>
#include <Stream.h>

namespace NightMare {

class Console {
public:
    explicit Console(ResourceManager& manager) : _router(manager) {}
    CommandRouter& router() { return _router; }
    CommandResult run(const String& line, CommandContext context = {});
    bool execute(const String& line);
    void tick(Stream& input);
private:
    CommandRouter _router;
    String _line;
    bool _discarding = false;
};

} // namespace NightMare
