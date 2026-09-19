#include <NightMare/Network/Dispatcher.h>

namespace NightMare {

bool Dispatcher::dispatch(const String& topic, const String& payload) {
    String prefix = "nm/" + _manager.nameSpace() + "/";
    if (!topic.startsWith(prefix)) return false;
    int ownerEnd = topic.indexOf('/', prefix.length());
    if (ownerEnd < 0) return false;
    String owner = topic.substring(prefix.length(), ownerEnd);
    if (!owner.length()) return false;
    String remainder = topic.substring(ownerEnd + 1);
    if (remainder == "reply") {
        if (owner == _manager.device()) _manager.receiveReply(payload);
        return true;
    }
    if (!remainder.startsWith("r/")) return false;
    int idEnd = remainder.indexOf('/', 2);
    if (idEnd < 3) return false;
    String id = remainder.substring(2, idEnd);
    String operation = remainder.substring(idEnd + 1);
    if (operation == "state") _manager.receive(owner, id, Operation::VALUE_STATE, payload);
    else if (operation == "write") _manager.receive(owner, id, Operation::VALUE_WRITE, payload);
    else if (operation == "invoke") _manager.receive(owner, id, Operation::ACTION_INVOKE, payload);
    else if (operation == "emit") _manager.receive(owner, id, Operation::EVENT_EMIT, payload);
    else if (operation != "schema") return false;
    return true;
}

} // namespace NightMare
